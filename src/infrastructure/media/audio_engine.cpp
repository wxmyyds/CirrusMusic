#include "audio_engine_producer_p.h"
#include "audio_decoder.h"
#include "audio_output.h"
#include "audio_store.h"
#include "automix/analysis.h"
#include "automix/analyst.h"
#include "automix/deck.h"
#include "automix/mix.h"
#include "automix/transition.h"
#include "logging/log.h"
#include "loudness.h"
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace cirrus::media
{
using namespace domain;
using namespace application;
using Kind = PlaybackEvent::Kind;
namespace
{
constexpr qsizetype kChunkFrames = 2048; // output frames the producer makes at a time
constexpr int kTickMs = 100;
constexpr qint64 kUnderrunLogMs = 10000; // underruns are summed over this in the log
constexpr int kOpenTimeoutMs = 30000;
constexpr int kSuspendAfterMs = 300; // past the fade-out
constexpr int kTailCheckMs = 100;    // a skip's tail still fading: suspend after it
// The next track: its first bytes are fetched as soon as it is known, the rest from
// this long before the current one ends (or once the current file is all here).
constexpr qint64 kPrefetchBytes = 1024 * 1024;
constexpr qint64 kOpenNextBeforeMs = 30000;
// A transition's next track is opened (and its transition planned) this long before
// the longest overlap could start.
constexpr qint64 kOpenAheadMs = 3000;
// With transitions the next track downloads this long before the current one ends (its
// start is analysed meanwhile: docs/tasks/automix.md §4.1), and the transition is planned
// this long before: its start may be well before the end (silence, a fade-out, an outro).
constexpr qint64 kOpenNextBeforeMixMs = 90000;
constexpr qint64 kPlanBeforeMs = 60000;
// Of the next track the start is analysed first: enough for its intro and first lines.
constexpr qint64 kNextAnalysisMs = 60000;

// The same file again (repeat one, the same song twice in a row): not mixed with itself.
// Compared by cache identity: content digest, or (Bilibili) the stream name.
bool sameFile(const MediaSource& a, const MediaSource& b)
{
    return hasIdentity(a) && a.identity == b.identity;
}

// What the plans go by. `cached`: kept from when it played before (`ms` 0).
void logAnalysis(LoadToken token, const automix::Analysis& a, qint64 ms, bool cached)
{
    log::info(lcMedia, "analysis.done",
              {{"token", token.generation},
               {"complete", a.complete},
               {"cached", cached},
               {"ms", ms},
               {"lufs", QString::number(a.loudness, 'f', 1)},
               {"sound_ms", a.soundStartMs},
               {"sound_end_ms", a.soundEndMs},
               {"fade_ms", a.fadeStartMs},
               {"bpm", QString::number(a.rhythm.bpm, 'f', 2)},
               {"steady", a.rhythm.steady},
               {"beats", qint64(a.rhythm.beats.size())},
               {"bar_confidence", QString::number(a.rhythm.barConfidence, 'f', 2)},
               log::fieldIf(a.startKey.valid(), "start_key", automix::keyName(a.startKey)),
               log::fieldIf(a.endKey.valid(), "end_key", automix::keyName(a.endKey))});
}
} // namespace

// The decoder thread and what it shares with the owner are in audio_engine_producer_p.h.

qsizetype AudioEngine::Producer::pullFrom(automix::Deck& deck, float* out, qsizetype frames)
{
    qsizetype got = deck.pull(out, frames, normalize);
    // An interruption that came after the seek it was for: go on where it was.
    for (int retry = 0; retry < 3 && got < frames && deck.decoder().interrupted() && undisturbed();
         ++retry) {
        deck.resume();
        got += deck.pull(out + got * 2, frames - got, normalize);
    }
    return got;
}

void AudioEngine::Producer::prepare(AudioEngine* engine)
{
    if (m_upcoming) {
        bool again;
        {
            std::lock_guard guard(lock);
            if (nextStream != m_upcoming->stream) {
                m_upcoming.reset(); // the queue changed: another next track, or none
                return;
            }
            again = std::exchange(replan, false);
        }
        if (again && !m_mixing)
            planAgain(engine);
        return;
    }
    if (m_mixing || m_cur->decoderEnded())
        return;
    std::shared_ptr<AudioStream> next;
    QString format;
    float gain;
    bool nextIsPreview, sameFile;
    LoadToken nextIdentity;
    automix::Settings settings;
    std::shared_ptr<const automix::Analysis> analysis, nextAnalysisNow;
    std::vector<automix::Span> vocals, nextVocalsNow;
    {
        std::lock_guard guard(lock);
        if (!nextStream || crossed)
            return;
        next = nextStream;
        nextIdentity = nextToken;
        format = nextFormat;
        gain = nextGain;
        nextIsPreview = nextPreview;
        sameFile = nextSameFile;
        settings = transition;
        analysis = curAnalysis;
        nextAnalysisNow = nextAnalysis;
        vocals = curVocals;
        nextVocalsNow = nextVocals;
        replan = false;
    }
    if (settings.mode == TransitionMode::Gapless)
        return; // opened at the end, as ever
    const qint64 position = m_cur->positionMs();
    if (m_curDurationMs - position > kPlanBeforeMs + kOpenAheadMs)
        return; // not yet
    auto decoder = std::make_unique<AudioDecoder>();
    if (!decoder->open(next, format)) {
        std::lock_guard guard(lock);
        if (!quit && !seekTo && nextStream == next) {
            nextFailed = true;
            nextStream.reset();
        }
        return;
    }
    auto plan = automix::plan(
        settings, {m_curDurationMs, preview, analysis.get(), std::move(vocals)}, position,
        {decoder->durationMs(), nextIsPreview, nextAnalysisNow.get(), std::move(nextVocalsNow)},
        sameFile);
    QMetaObject::invokeMethod(
        engine, [engine, mode = settings.mode, plan] { engine->planned(mode, plan, false); },
        Qt::QueuedConnection);
    m_upcoming =
        Upcoming{std::move(next), std::move(decoder), nextIdentity, gain, nextIsPreview,
                 std::move(plan)};
}

void AudioEngine::Producer::planAgain(AudioEngine* engine)
{
    automix::Settings settings;
    std::shared_ptr<const automix::Analysis> analysis, nextAnalysisNow;
    std::vector<automix::Span> vocals, nextVocalsNow;
    bool sameFile;
    {
        std::lock_guard guard(lock);
        settings = transition;
        analysis = curAnalysis;
        nextAnalysisNow = nextAnalysis;
        vocals = curVocals;
        nextVocalsNow = nextVocals;
        sameFile = nextSameFile;
    }
    const qint64 position = m_cur->positionMs();
    auto plan = automix::plan(
        settings, {m_curDurationMs, preview, analysis.get(), std::move(vocals)}, position,
        {m_upcoming->decoder->durationMs(), m_upcoming->preview, nextAnalysisNow.get(),
         std::move(nextVocalsNow)},
        sameFile);
    // Only while it can still start where it says (a gapless one always can).
    if (plan.style != automix::Style::Gapless && plan.outMs < position + automix::kStartMarginMs)
        return;
    QMetaObject::invokeMethod(
        engine, [engine, mode = settings.mode, plan] { engine->planned(mode, plan, true); },
        Qt::QueuedConnection);
    m_upcoming->plan = std::move(plan);
}

bool AudioEngine::Producer::startMixing()
{
    auto deck = std::make_unique<automix::Deck>(std::move(m_upcoming->decoder), outRate,
                                                m_upcoming->gain, normalize);
    const automix::Plan& plan = m_upcoming->plan;
    if (plan.inMs > 0 && !deck->seek(plan.inMs)) {
        m_upcoming.reset(); // no transition: the current track ends, the next is opened again
        return false;
    }
    const qint64 durationMs = deck->decoder().durationMs();
    const quint64 start = produced;
    {
        std::lock_guard guard(lock);
        if (quit || seekTo || nextStream != m_upcoming->stream) {
            m_upcoming.reset();
            return false;
        }
        crossed = true;
        nextStartFrame = start + quint64(std::max<qint64>(0, plan.inDelayMs) * outRate / 1000);
        nextBaseMs = plan.inMs;
        boundary = start + quint64(plan.switchMs * outRate / 1000);
        nextDurationMs = durationMs;
    }
    const qint64 length = plan.overlapMs * outRate / 1000;
    automix::Mixer mixer(m_upcoming->plan, outRate);
    m_mixing =
        Mixing{std::move(deck), m_upcoming->token, std::move(m_upcoming->plan), std::move(mixer),
               durationMs, start, 0, length};
    m_outgoingFailed = false;
    m_upcoming.reset();
    return true;
}

bool AudioEngine::Producer::mixSome(AudioEngine* engine, std::vector<float>& out)
{
    Mixing& mix = *m_mixing;
    const auto frames = qsizetype(std::min<qint64>(kChunkFrames, mix.length - mix.offset));
    out.assign(size_t(frames) * 2, 0.0f);
    m_other.assign(size_t(frames) * 2, 0.0f);
    qsizetype a = m_outgoingFailed ? 0 : pullFrom(*m_cur, out.data(), frames);
    if (const auto error = m_cur->decoder().error(); error && !m_outgoingFailed) {
        m_outgoingFailed = true;
        failed(engine, m_curToken, *error);
    }
    if (!m_outgoingFailed && a < frames && m_cur->decoderEnded()) {
        m_cur->finish(); // the current track ends inside the overlap: silent for the rest
        a += pullFrom(*m_cur, out.data() + a * 2, frames - a);
    }
    const qsizetype silent = mix.mixer.silentIn(frames); // the next track begins after these
    const qsizetype b = silent + pullFrom(*mix.deck, m_other.data() + silent * 2, frames - silent);
    if (const auto error = mix.deck->decoder().error()) {
        failed(engine, mix.token, *error);
        out.clear();
        return false;
    }
    if ((a < frames && m_cur->decoder().interrupted()) ||
        (b < frames && mix.deck->decoder().interrupted())) {
        out.clear(); // a seek or stop is waiting: this piece never plays
        return true;
    }
    mix.mixer.mix(out.data(), m_other.data(), out.data(), frames);
    mix.offset += frames;
    if (mix.offset < mix.length)
        return true;
    // Over: the next track is the current one now, the old one's deck goes.
    m_curToken = mix.token;
    m_cur = std::move(mix.deck);
    m_curDurationMs = mix.durationMs;
    m_curOrigin = mix.startFrame;
    preview = false; // previews are never mixed
    m_mixing.reset();
    return true;
}

bool AudioEngine::Producer::crossNext()
{
    std::shared_ptr<AudioStream> next;
    std::unique_ptr<AudioDecoder> opened;
    float gain;
    bool nextIsPreview;
    LoadToken nextIdentity;
    if (m_upcoming) {
        // Opened ahead for a transition that didn't start (the track ended early, or the
        // plan was gapless): it follows right here.
        next = std::move(m_upcoming->stream);
        opened = std::move(m_upcoming->decoder);
        gain = m_upcoming->gain;
        nextIsPreview = m_upcoming->preview;
        nextIdentity = m_upcoming->token;
        m_upcoming.reset();
    } else {
        QString format;
        {
            std::lock_guard guard(lock);
            if (crossed || !nextStream || seekTo || quit)
                return false;
            next = nextStream;
            nextIdentity = nextToken;
            format = nextFormat;
            gain = nextGain;
            nextIsPreview = nextPreview;
        }
        opened = std::make_unique<AudioDecoder>();
        if (!opened->open(next, format)) {
            std::lock_guard guard(lock);
            if (!quit && !seekTo && nextStream == next) {
                nextFailed = true;
                nextStream.reset();
            }
            return false;
        }
    }
    const qint64 durationMs = opened->durationMs();
    std::lock_guard guard(lock);
    if (quit || seekTo || crossed || nextStream != next)
        return false; // the seek restarts the current track's deck
    boundary = m_curOrigin + quint64(m_cur->follow(std::move(opened), gain));
    m_curToken = nextIdentity;
    crossed = true;
    nextStartFrame = boundary;
    nextBaseMs = 0;
    nextDurationMs = durationMs;
    m_curDurationMs = durationMs;
    preview = nextIsPreview;
    return true;
}

bool AudioEngine::Producer::crossLate()
{
    std::shared_ptr<AudioStream> next;
    QString format;
    float gain;
    bool nextIsPreview;
    {
        std::lock_guard guard(lock);
        if (crossed || !nextStream || seekTo || quit)
            return false;
        next = nextStream;
        format = nextFormat;
        gain = nextGain;
        nextIsPreview = nextPreview;
    }
    auto opened = std::make_unique<AudioDecoder>();
    if (!opened->open(next, format)) {
        std::lock_guard guard(lock);
        if (!quit && !seekTo && nextStream == next) {
            nextFailed = true;
            nextStream.reset();
        }
        return false;
    }
    const qint64 durationMs = opened->durationMs();
    std::lock_guard guard(lock);
    if (quit || seekTo || crossed || nextStream != next)
        return false;
    m_cur = std::make_unique<automix::Deck>(std::move(opened), outRate, gain, normalize);
    m_curToken = nextToken; // still locked: identity and decoder transfer together
    m_curOrigin = produced;
    m_curDurationMs = durationMs;
    preview = nextIsPreview;
    crossed = true;
    boundary = nextStartFrame = m_curOrigin;
    nextBaseMs = 0;
    nextDurationMs = durationMs;
    return true;
}

void AudioEngine::Producer::failed(AudioEngine* engine, LoadToken token, PlaybackError error)
{
    if (quitting())
        return; // stopping interrupted it; not a playback failure
    QMetaObject::invokeMethod(engine, [engine, origin = weak_from_this(), token, error] {
        if (auto producer = origin.lock())
            engine->producerFailed(producer, token, error);
    }, Qt::QueuedConnection);
}

void AudioEngine::Producer::run(AudioEngine* engine, LoadToken token, bool reopening)
{
    m_curToken = token;
    auto decoder = std::make_unique<AudioDecoder>();
    if (!decoder->open(stream, format)) {
        failed(engine, m_curToken, decoder->error().value_or(PlaybackError::Decode));
        return;
    }
    if (!reopening) {
        const qint64 duration = decoder->durationMs();
        QMetaObject::invokeMethod(
            engine, [engine, token, duration] { engine->opened(token, duration); },
            Qt::QueuedConnection);
    }
    m_curDurationMs = decoder->durationMs();
    m_cur = std::make_unique<automix::Deck>(std::move(decoder), outRate, trackGain, normalize);
    AudioRing& ring = *output->ring();
    std::vector<float> out;
    size_t written = 0;   // of `out`
    bool flushing = true; // the owner flushed before starting us
    int seekRetries = 0;  // of an interrupted seek nothing newer replaced
    while (true) {
        std::optional<qint64> seek;
        {
            std::lock_guard guard(lock);
            if (quit)
                return;
            seek = std::exchange(seekTo, std::nullopt);
            if (seek)
                seeking = seek;
        }
        if (seek) {
            if (!flushing)
                output->beginFlush(FlushTail::Cut);
            flushing = true;
            out.clear();
            written = 0;
            // The owner took the next track over while the transition still sounded (it
            // restarts us for a seek before that): the seek is in the next track.
            if (m_mixing) {
                m_curToken = m_mixing->token;
                m_cur = std::move(m_mixing->deck);
                m_curDurationMs = m_mixing->durationMs;
                m_mixing.reset();
            }
            m_upcoming.reset(); // opened again when the time comes
            if (!m_cur->seek(*seek)) {
                if (m_cur->decoder().interrupted()) {
                    // A newer seek or a stop goes next. Else the interruption was the
                    // owner waking a waiting read for this very seek (AudioEngine::seek),
                    // and nothing read was waiting: it cut the seek's own read short. Once
                    // more, then (2026-11-22: the seek was dropped, the bar sprang back).
                    std::lock_guard guard(lock);
                    if (seekTo)
                        seekRetries = 0;
                    else if (!quit && seekRetries < 3) {
                        ++seekRetries;
                        seekTo = seek;
                        log::debug(lcMedia, "engine.seek_again", {"try", seekRetries});
                    } else
                        seeking.reset();
                    continue;
                }
                failed(engine, m_curToken, m_cur->decoder().error().value_or(PlaybackError::Decode));
                return;
            }
            seekRetries = 0;
            produced = 0;
            eof = false;
            m_curOrigin = 0;
            std::lock_guard guard(lock);
            seeking.reset();
            baseMs = *seek;
            startFrame = 0;
            ++seekSerial;
            continue;
        }
        // Interrupted for a seek or stop that isn't (or no longer) here.
        m_cur->resume();
        if (m_mixing)
            m_mixing->deck->resume();
        if (written < out.size()) {
            const auto frames =
                ring.write(out.data() + written, qsizetype(out.size() - written) / 2);
            written += size_t(frames) * 2;
            produced += quint64(frames);
            if (flushing) {
                output->endFlush(); // the first audio is in: the consumer may go on
                flushing = false;
            }
            if (written < out.size()) {
                std::unique_lock guard(lock);
                wake.wait_for(guard, std::chrono::milliseconds(5),
                              [this] { return quit || seekTo.has_value(); });
            }
            continue;
        }
        out.clear();
        written = 0;
        if (eof) {
            if (crossLate()) {
                eof = false;
                continue;
            }
            std::unique_lock guard(lock);
            wake.wait_for(guard, std::chrono::milliseconds(20),
                          [this] { return quit || seekTo.has_value(); });
            continue;
        }
        prepare(engine);
        if (m_mixing) {
            if (!mixSome(engine, out))
                return;
            continue;
        }
        // The current track alone, up to the transition's start when one is planned.
        qsizetype frames = kChunkFrames;
        if (m_upcoming && m_upcoming->plan.style != automix::Style::Gapless) {
            const qint64 until = m_cur->framesUntil(m_upcoming->plan.outMs);
            if (until <= 0) {
                startMixing();
                continue;
            }
            frames = qsizetype(std::min<qint64>(frames, until));
        }
        out.resize(size_t(frames) * 2);
        const qsizetype got = pullFrom(*m_cur, out.data(), frames);
        out.resize(size_t(got) * 2);
        if (got == frames)
            continue;
        if (const auto error = m_cur->decoder().error()) {
            failed(engine, m_curToken, *error);
            return;
        }
        if (!m_cur->decoderEnded())
            continue; // a seek or stop is waiting
        if (crossNext())
            continue; // on into the next track, no gap
        m_cur->finish();
        std::vector<float> tail(size_t(kChunkFrames) * 2);
        while (const qsizetype more = m_cur->pull(tail.data(), kChunkFrames, normalize))
            out.insert(out.end(), tail.begin(), tail.begin() + qsizetype(more) * 2);
        eof = true;
        if (out.empty() && flushing) {
            output->endFlush();
            flushing = false;
        }
    }
}

AudioEngine::AudioEngine(AudioStore& store, QObject* parent)
    : IPlaybackBackend(parent), m_store(store), m_output(std::make_unique<AudioOutput>()),
      m_analyst(std::make_unique<automix::Analyst>())
{
    connect(m_analyst.get(), &automix::Analyst::analysed, this, &AudioEngine::analysed);
    m_ticker.setInterval(kTickMs);
    m_ticker.setTimerType(Qt::PreciseTimer);
    connect(&m_ticker, &QTimer::timeout, this, &AudioEngine::tick);
    m_openDeadline.setSingleShot(true);
    connect(&m_openDeadline, &QTimer::timeout, this, [this] {
        if (m_active && !m_ready)
            fail(m_token, PlaybackError::Timeout);
    });
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        if (m_playing)
            return;
        if (m_output->fading())
            m_idle.start(kTailCheckMs); // or its rest would play after the next resume()
        else
            m_output->suspend();
    });
    connect(m_output.get(), &AudioOutput::deviceChanged, this, &AudioEngine::deviceChanged);
    connect(&m_store, &AudioStore::urlNeeded, this, &AudioEngine::urlNeeded);
}

AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::send(PlaybackEvent value)
{
    if (m_active && value.token == m_token)
        emit event(std::move(value));
}

void AudioEngine::sendLater(PlaybackEvent value)
{
    // Like the old backend's queued signals: never re-enter the caller of play()/pause().
    QMetaObject::invokeMethod(this, [this, value] { send(value); }, Qt::QueuedConnection);
}

void AudioEngine::load(MediaSource source, LoadToken token)
{
    Q_ASSERT(QThread::currentThread() == thread());
    stop();
    m_token = token;
    m_active = true;
    m_source = std::move(source);
    m_clock.start();
    log::debug(lcMedia, "engine.load",
               {{"token", token.generation},
                {"format", m_source.format},
                {"preview", m_source.preview.has_value()},
                {"gain_db", m_source.gainDb ? QString::number(*m_source.gainDb, 'f', 1)
                                            : QStringLiteral("-")}});
    PlaybackError error{};
    m_stream = m_store.open(m_source, &error);
    if (!m_stream) {
        fail(token, error);
        return;
    }
    if (!m_output->isOpen() && !m_output->open()) {
        fail(token, PlaybackError::NoOutput);
        return;
    }
    m_output->setGain(m_volume);
    m_output->setPlaying(false);
    m_openDeadline.start(kOpenTimeoutMs);
    startProducer(0);
}

void AudioEngine::startProducer(qint64 positionMs, bool seek)
{
    auto producer = std::make_shared<Producer>();
    producer->output = m_output.get();
    producer->outRate = m_output->sampleRate();
    producer->stream = m_stream;
    producer->format = m_source.format.toLower();
    producer->preview = m_source.preview.has_value();
    producer->transition = m_transition;
    producer->curAnalysis = m_analysis;
    producer->curVocals = m_vocals;
    if (m_next) {
        producer->nextAnalysis = m_next->analysis;
        producer->nextVocals = m_next->vocals;
    }
    producer->baseMs = positionMs;
    if (positionMs > 0 || seek)
        producer->seekTo = positionMs;
    if (m_next && m_next->stream) {
        m_next->stream->clearInterrupt();
        producer->nextStream = m_next->stream;
        producer->nextToken = m_next->token;
        producer->nextFormat = m_next->source.format.toLower();
        producer->nextGain = normalizationGain(m_next->source);
        producer->nextPreview = m_next->source.preview.has_value();
        producer->nextSameFile = sameFile(m_source, m_next->source);
    }
    // New audio after the flush: at its gain from the first frame.
    producer->normalize = m_normalize;
    producer->trackGain = normalizationGain(m_source);
    m_output->beginFlush();     // nothing old plays; the producer ends it with new audio
    m_stream->clearInterrupt(); // left over from stopping the previous producer
    const bool reopening = m_ready;
    Producer* raw = producer.get();
    producer->thread = std::thread([raw, engine = this, token = m_token, reopening] {
        QThread::currentThread()->setObjectName(QStringLiteral("decode")); // in the log
        raw->run(engine, token, reopening);
    });
    m_producer = std::move(producer);
    m_seekSerial = 0;
}

void AudioEngine::stopProducer(FlushTail tail)
{
    if (!m_producer)
        return;
    {
        std::lock_guard guard(m_producer->lock);
        m_producer->quit = true;
    }
    m_producer->wake.notify_all();
    if (m_stream)
        m_stream->interrupt(); // a read waiting for the download returns
    if (m_next && m_next->stream)
        m_next->stream->interrupt(); // or it may be reading the next track
    if (m_producer->thread.joinable())
        m_producer->thread.join();
    m_producer.reset();
    m_output->beginFlush(tail); // what it queued never plays, or fades out quickly
}

void AudioEngine::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    // A skip (or the queue stopped) while playing: a quick fade-out; the gapless mode keeps
    // to a few ms (docs/tasks/automix.md §3).
    const auto tail =
        m_transition.mode == TransitionMode::Gapless ? FlushTail::Cut : FlushTail::Skip;
    m_active = false;
    m_ready = false;
    m_playing = false;
    m_ended = false;
    m_durationMs = 0;
    m_lastPosition = -1;
    m_ticker.stop();
    m_openDeadline.stop();
    m_meter.reset();
    m_published.reset();
    stopProducer(tail);
    cancelAnalysis(m_curReader, m_curJob);
    m_analysis.reset();
    m_curFull = false;
    m_curLookedUp = false;
    m_vocals.clear();
    if (m_next)
        cancelAnalysis(m_next->reader, m_next->job);
    m_next.reset();
    if (m_stream) {
        m_stream->cancel();
        m_stream.reset(); // releasing it stops its download; what came stays cached
    }
    m_output->setPlaying(false);
    m_idle.start(kSuspendAfterMs);
}

qint64 AudioEngine::clipLength() const
{
    if (m_source.preview)
        return std::min(m_durationMs, m_source.preview->endMs - m_source.preview->startMs);
    return m_durationMs;
}

qint64 AudioEngine::playedFrames() const
{
    return std::max<qint64>(0, qint64(m_output->consumed()) - m_output->latencyFrames());
}

qint64 AudioEngine::position() const
{
    if (!m_producer || m_output->sampleRate() <= 0)
        return 0;
    qint64 base;
    quint64 start;
    std::optional<qint64> pending;
    {
        std::lock_guard guard(m_producer->lock);
        base = m_producer->baseMs;
        start = m_producer->startFrame;
        pending = m_producer->seekTo ? m_producer->seekTo : m_producer->seeking;
    }
    if (pending)
        return *pending; // the seek hasn't taken effect yet
    const qint64 played = std::max<qint64>(0, playedFrames() - qint64(start));
    return std::min(clipLength(), base + played * 1000 / m_output->sampleRate());
}

void AudioEngine::opened(LoadToken token, qint64 durationMs)
{
    if (!m_active || token != m_token || m_ready)
        return;
    m_openDeadline.stop();
    if (m_source.preview &&
        durationMs > m_source.preview->endMs - m_source.preview->startMs + 250) {
        fail(token, PlaybackError::InvalidResponse);
        return;
    }
    m_durationMs = durationMs;
    m_ready = true;
    log::debug(
        lcMedia, "engine.ready",
        {{"token", token.generation}, {"duration_ms", durationMs}, {"open_ms", m_clock.elapsed()}});
    m_ticker.start();
    send({token, Kind::Duration, {}, durationMs});
    if (!m_active || token != m_token)
        return;
    send({token, Kind::Seekable, {}, 1});
    if (!m_active || token != m_token)
        return;
    PlaybackEvent volume{token, Kind::Volume};
    volume.volumeLinear = m_volume;
    send(volume);
    if (!m_active || token != m_token)
        return;
    send({token, Kind::Ready});
    if (m_active && token == m_token)
        openNext();
}

void AudioEngine::tick()
{
    if (!m_active || !m_ready || m_ended || !m_producer)
        return;
    const auto token = m_token;
    qint64 base;
    quint64 serial;
    bool seeking, crossed, nextFailed;
    quint64 boundary, nextStart;
    qint64 nextDuration, nextBase;
    {
        std::lock_guard guard(m_producer->lock);
        base = m_producer->baseMs;
        serial = m_producer->seekSerial;
        seeking = m_producer->seekTo || m_producer->seeking;
        crossed = m_producer->crossed;
        boundary = m_producer->boundary;
        nextStart = m_producer->nextStartFrame;
        nextBase = m_producer->nextBaseMs;
        nextDuration = m_producer->nextDurationMs;
        nextFailed = std::exchange(m_producer->nextFailed, false);
    }
    if (serial != m_seekSerial) {
        m_seekSerial = serial;
        m_meter.breakInterval();
        m_lastPosition = base;
        send({token, Kind::SeekConfirmed, {}, base});
        if (!m_active || token != m_token)
            return;
    }
    if (nextFailed)
        m_next.reset(); // this track just ends; the next is loaded the usual way
    if (crossed && !seeking && quint64(playedFrames()) >= boundary) {
        advance(nextDuration, nextStart, nextBase);
        return;
    }
    openNext();
    analyseCurrent();
    const qint64 now = position();
    const bool drained = m_producer->eof && m_output->consumed() >= m_producer->produced;
    if ((m_source.preview && now >= clipLength()) || drained) {
        ended();
        return;
    }
    if (!seeking && now != m_lastPosition) {
        m_lastPosition = now;
        send({token, Kind::Position, {}, now});
        if (!m_active || token != m_token)
            return;
    }
    const auto underruns = m_output->underruns();
    const bool stalled = underruns != m_underruns;
    m_underruns = underruns;
    // Counted on the audio thread, reported from here (docs/tasks/logging.md §3).
    if (!m_underrunWindow.isValid())
        m_underrunWindow.start();
    if (m_underrunWindow.elapsed() >= kUnderrunLogMs) {
        if (underruns > m_underrunsLogged)
            log::warn(lcMedia, "engine.underruns",
                      {{"token", token.generation},
                       {"count", underruns - m_underrunsLogged},
                       {"window_ms", m_underrunWindow.elapsed()}});
        m_underrunsLogged = underruns;
        m_underrunWindow.restart();
    }
    const bool eligible = m_playing && !seeking && !stalled && m_volume > 0 && !m_output->silent();
    const auto progress = m_meter.sample(now, m_clock.elapsed(), eligible);
    if (m_published == progress)
        return;
    m_published = progress;
    PlaybackEvent observation{token, Kind::Observation};
    observation.observed = progress;
    send(observation); // last: observers may retire this load synchronously
}

void AudioEngine::ended()
{
    if (!m_active || m_ended)
        return;
    m_ended = true;
    log::debug(lcMedia, "engine.ended", {"token", m_token.generation});
    m_ticker.stop();
    m_meter.breakInterval();
    m_playing = false;
    m_output->setPlaying(false);
    m_idle.start(kSuspendAfterMs);
    send({m_token, Kind::Ended});
}

void AudioEngine::producerFailed(const std::shared_ptr<Producer>& producer, LoadToken token,
                                 PlaybackError error)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_active || producer != m_producer)
        return; // a stopped/replaced producer's queued callback cannot fail a new load
    if (m_next && token == m_next->token) {
        qint64 duration, base;
        quint64 start;
        {
            std::lock_guard guard(producer->lock);
            if (!producer->crossed)
                return; // no decoder ownership transfer happened
            duration = producer->nextDurationMs;
            start = producer->nextStartFrame;
            base = producer->nextBaseMs;
        }
        // Decode can outrun the sink and its ticker. Publish the identity transfer before
        // Failure even when the sink has not reached the planned boundary yet.
        advance(duration, start, base);
        if (!m_active || m_producer != producer || m_token != token)
            return; // Advanced observers may synchronously replace the load
    }
    if (token == m_token)
        fail(token, error);
    else
        // The outgoing deck can fail while the owner is already on the incoming track.
        // Report its actual identity; the producer leaves that side silent and continues.
        emit event({token, Kind::Failure, {}, 0, {error}});
}

void AudioEngine::fail(LoadToken token, PlaybackError error)
{
    if (!m_active || token != m_token)
        return;
    log::warn(lcMedia, "engine.failed",
              {{"token", token.generation},
               {"err", error},
               {"ready", m_ready},
               {"ms", m_clock.elapsed()}});
    m_meter.breakInterval();
    m_ticker.stop();
    QTimer::singleShot(0, this, [this, producer = m_producer, token, error] {
        if (!m_active || token != m_token || producer != m_producer)
            return;
        stop();
        emit event({token, Kind::Failure, {}, 0, {error}});
    });
}

void AudioEngine::preload(MediaSource source, LoadToken next)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_active)
        return;
    dropNext();
    if (!next.generation)
        return;
    m_next = Next{std::move(source), next, {}};
    m_store.prefetch(m_next->source, kPrefetchBytes);
    openNext();
}

void AudioEngine::openNext()
{
    if (!m_next || m_next->stream || !m_ready || m_ended || !m_producer || m_source.preview)
        return; // a preview ends at its clip, not its file: the next loads the usual way
    const qint64 before =
        m_transition.mode == TransitionMode::Gapless ? kOpenNextBeforeMs : kOpenNextBeforeMixMs;
    const bool soon = m_durationMs > 0 && m_durationMs - position() < before;
    if (!soon && !(m_stream && m_stream->complete()))
        return;
    PlaybackError error{};
    m_next->stream = m_store.open(m_next->source, &error);
    if (!m_next->stream) {
        log::warn(lcMedia, "engine.next_open_failed",
                  {{"token", m_next->token.generation}, {"err", error}});
        m_next.reset();
        return;
    }
    log::debug(lcMedia, "engine.next_opened",
               {{"token", m_next->token.generation}, {"left_ms", m_durationMs - position()}});
    {
        std::lock_guard guard(m_producer->lock);
        m_producer->nextStream = m_next->stream;
        m_producer->nextToken = m_next->token;
        m_producer->nextFormat = m_next->source.format.toLower();
        m_producer->nextGain = normalizationGain(m_next->source);
        m_producer->nextPreview = m_next->source.preview.has_value();
        m_producer->nextSameFile = sameFile(m_source, m_next->source);
    }
    analyseNext();
}

void AudioEngine::dropNext()
{
    if (!m_next)
        return;
    bool crossed = false;
    if (m_producer) {
        std::lock_guard guard(m_producer->lock);
        crossed = m_producer->crossed;
        if (!crossed)
            m_producer->nextStream.reset();
    }
    cancelAnalysis(m_next->reader, m_next->job);
    if (crossed) {
        // Already decoding it: decode this track again from where it plays.
        const qint64 at = position();
        stopProducer(FlushTail::Cut);
        m_next.reset();
        startProducer(at, true);
        return;
    }
    if (m_next->stream)
        m_next->stream->interrupt(); // the producer may be opening it
    m_next.reset();
    handPlanInputs();
}

void AudioEngine::advance(qint64 durationMs, quint64 startFrame, qint64 baseMs)
{
    // Playback has reached the next track (the joint, or a transition's switch point): it
    // is the current one now. Its audio started at `startFrame`, from its time `baseMs`.
    {
        std::lock_guard guard(m_producer->lock);
        m_producer->crossed = false;
        m_producer->nextStream.reset();
        m_producer->startFrame = startFrame;
        m_producer->baseMs = baseMs;
    }
    m_stream = std::move(m_next->stream); // the old one's download stops; it stays cached
    m_source = std::move(m_next->source);
    m_token = m_next->token;
    // Its analysis so far (of its start, while that runs: then of all of it).
    cancelAnalysis(m_curReader, m_curJob);
    m_analysis = std::move(m_next->analysis);
    m_curJob = m_next->job;
    m_curReader = std::move(m_next->reader);
    m_curFull = m_analysis && m_analysis->complete;
    m_curLookedUp = false; // (it wasn't, if the next opened while gapless)
    m_vocals = std::move(m_next->vocals);
    m_next.reset();
    handPlanInputs();
    m_durationMs = durationMs;
    m_lastPosition = -1;
    m_meter.reset();
    m_published.reset();
    const auto token = m_token;
    log::debug(lcMedia, "engine.advanced",
               {{"token", token.generation}, {"duration_ms", durationMs}});
    send({token, Kind::Advanced});
    if (!m_active || token != m_token)
        return;
    send({token, Kind::Duration, {}, durationMs});
    if (!m_active || token != m_token)
        return;
    send({token, Kind::Seekable, {}, 1});
}

void AudioEngine::refresh(LoadToken token, MediaSource source)
{
    std::shared_ptr<AudioStream> stream;
    if (m_active && token == m_token)
        stream = m_stream;
    else if (m_active && m_next && token == m_next->token) {
        m_next->source.url = source.url;
        m_next->source.validUntil = source.validUntil;
        stream = m_next->stream;
    }
    if (stream)
        m_store.refresh(stream->id(), source.url, source.validUntil);
}

void AudioEngine::urlNeeded(quint64 stream)
{
    if (!m_active)
        return;
    log::info(lcMedia, "engine.source_expired",
              {{"dl", stream}, {"next", !(m_stream && m_stream->id() == stream)}});
    if (m_stream && m_stream->id() == stream)
        send({m_token, Kind::SourceExpired});
    else if (m_next && m_next->stream && m_next->stream->id() == stream)
        emit event({m_next->token, Kind::SourceExpired});
}

void AudioEngine::play()
{
    if (!m_active || !m_ready || m_ended)
        return;
    m_playing = true;
    m_idle.stop();
    m_meter.breakInterval();
    m_output->resume();
    m_output->setPlaying(true);
    sendLater({m_token, Kind::State, PlaybackState::Playing});
}

void AudioEngine::pause()
{
    m_meter.breakInterval();
    if (!m_active)
        return;
    m_playing = false;
    m_output->setPlaying(false);
    m_idle.start(kSuspendAfterMs);
    if (m_ready)
        sendLater({m_token, Kind::State, PlaybackState::Paused});
}

void AudioEngine::seek(qint64 positionMs)
{
    if (!m_active || !m_ready || m_ended || !m_producer)
        return;
    const auto target = std::clamp<qint64>(positionMs, 0, std::max<qint64>(0, clipLength() - 1));
    log::debug(lcMedia, "engine.seek", {{"token", m_token.generation}, {"ms", target}});
    m_meter.breakInterval();
    bool crossed;
    {
        std::lock_guard guard(m_producer->lock);
        crossed = m_producer->crossed;
        if (!crossed)
            m_producer->seekTo = target;
    }
    if (crossed) {
        // The producer is on the next track already: decode this one again from there.
        stopProducer(FlushTail::Cut);
        startProducer(target, true);
        return;
    }
    m_producer->wake.notify_all();
    if (m_stream)
        m_stream->interrupt(); // don't wait out a download where we no longer play
}

void AudioEngine::setNormalization(bool on)
{
    if (on == m_normalize)
        return;
    m_normalize = on;
    if (m_producer)
        m_producer->normalize = on; // from the next decoded chunk, sliding
    log::info(lcMedia, "engine.normalize", {"on", on});
}

void AudioEngine::setTransition(TransitionMode mode, qint64 crossfadeMs)
{
    const automix::Settings settings{
        mode, std::clamp<qint64>(crossfadeMs, 1000, automix::kMaximumOverlapMs)};
    if (settings.mode == m_transition.mode && settings.crossfadeMs == m_transition.crossfadeMs)
        return;
    m_transition = settings;
    if (m_producer) {
        std::lock_guard guard(m_producer->lock);
        m_producer->transition = settings; // planned again while it hasn't started
        m_producer->replan = true;
    }
    log::info(lcMedia, "engine.transition",
              {{"mode", mode}, {"crossfade_ms", settings.crossfadeMs}});
}

void AudioEngine::planned(TransitionMode mode, const automix::Plan& plan, bool again)
{
    if (!m_active)
        return;
    log::info(lcMedia, "mix.plan",
              {{"token", m_token.generation},
               {"next", m_next ? m_next->token.generation : 0},
               {"mode", mode},
               {"style", plan.style},
               {"reason", plan.reason},
               {"out_ms", plan.outMs},
               {"in_ms", plan.inMs},
               {"overlap_ms", plan.overlapMs},
               log::fieldIf(plan.bars > 0, "bars", plan.bars),
               log::fieldIf(plan.inDelayMs > 0, "delay_ms", plan.inDelayMs),
               {"on_beat", plan.onBeat},
               log::fieldIf(plan.harmony != automix::Harmony::Unknown, "harmony", plan.harmony),
               {"again", again}});
}

void AudioEngine::analyseCurrent()
{
    if (m_curFull || m_curReader || !m_stream || !m_active || m_source.preview ||
        !hasIdentity(m_source) || m_transition.mode == TransitionMode::Gapless)
        return;
    if (!std::exchange(m_curLookedUp, true)) {
        if (auto kept = keptAnalysis(m_source)) { // analysed when it played before
            logAnalysis(m_token, *kept, 0, true);
            m_analysis = std::move(kept);
            m_curFull = true;
            handPlanInputs();
            return;
        }
    }
    if (!m_stream->complete())
        return; // all of it, from the cached file: nothing more to download
    PlaybackError error{};
    m_curReader = m_store.open(m_source, &error);
    if (!m_curReader)
        return;
    m_curFull = true;
    m_curJob = ++m_jobs;
    m_analyst->analyse(m_curJob, m_curReader, m_source.format.toLower());
}

void AudioEngine::analyseNext()
{
    // Its start while it downloads (all of it when it's cached already).
    if (!m_next || m_next->reader || m_next->analysis || m_next->source.preview ||
        !hasIdentity(m_next->source) || m_transition.mode == TransitionMode::Gapless)
        return;
    if (auto kept = keptAnalysis(m_next->source)) {
        logAnalysis(m_next->token, *kept, 0, true);
        m_next->analysis = std::move(kept);
        handPlanInputs();
        return;
    }
    PlaybackError error{};
    m_next->reader = m_store.open(m_next->source, &error);
    if (!m_next->reader)
        return;
    m_next->job = ++m_jobs;
    m_analyst->analyse(m_next->job, m_next->reader, m_next->source.format.toLower(),
                       m_next->reader->complete() ? 0 : kNextAnalysisMs);
}

void AudioEngine::analysed(quint64 id, std::shared_ptr<const automix::Analysis> analysis,
                           qint64 elapsedMs)
{
    const bool current = m_curReader && id == m_curJob;
    const bool next = m_next && m_next->reader && id == m_next->job;
    if (!current && !next)
        return;
    const LoadToken token = current ? m_token : m_next->token;
    if (!analysis)
        log::warn(lcMedia, "analysis.failed", {"token", token.generation});
    else
        logAnalysis(token, *analysis, elapsedMs, false);
    if (analysis && analysis->complete) // for the next time it plays
        m_store.keepAnalysis(current ? m_source : m_next->source, analysis->serialize());
    if (current) {
        m_curReader.reset();
        if (analysis)
            m_analysis = std::move(analysis);
    } else {
        m_next->reader.reset();
        if (analysis)
            m_next->analysis = std::move(analysis);
    }
    handPlanInputs();
}

std::shared_ptr<const automix::Analysis> AudioEngine::keptAnalysis(const MediaSource& source) const
{
    const QByteArray bytes = m_store.analysis(source);
    if (bytes.isEmpty())
        return nullptr;
    auto analysis = automix::Analysis::deserialize(bytes);
    if (!analysis || !analysis->complete)
        return nullptr; // made by another version: analysed again
    return std::make_shared<const automix::Analysis>(std::move(*analysis));
}

void AudioEngine::cancelAnalysis(std::shared_ptr<AudioStream>& reader, quint64 job)
{
    if (!reader)
        return;
    m_analyst->cancel(job);
    reader->cancel(); // a read waiting for the download returns
    reader.reset();
}

void AudioEngine::handPlanInputs()
{
    if (!m_producer)
        return;
    std::lock_guard guard(m_producer->lock);
    m_producer->curAnalysis = m_analysis;
    m_producer->nextAnalysis = m_next ? m_next->analysis : nullptr;
    m_producer->curVocals = m_vocals;
    m_producer->nextVocals = m_next ? m_next->vocals : std::vector<automix::Span>{};
    m_producer->replan = true;
}

void AudioEngine::setVocals(LoadToken token, const QList<VocalSpan>& spans)
{
    std::vector<automix::Span> lines;
    lines.reserve(size_t(spans.size()));
    for (const VocalSpan& span : spans)
        lines.push_back({span.startMs, span.endMs});
    if (m_active && token == m_token)
        m_vocals = std::move(lines);
    else if (m_next && token == m_next->token)
        m_next->vocals = std::move(lines);
    else
        return;
    log::debug(lcMedia, "engine.vocals", {{"token", token.generation}, {"lines", spans.size()}});
    handPlanInputs();
}

void AudioEngine::setVolume(float linear)
{
    if (!std::isfinite(linear) || linear < 0 || linear > 1)
        return;
    if ((linear == 0) != (m_volume == 0))
        m_meter.breakInterval();
    m_volume = linear;
    m_output->setGain(linear);
    if (m_active && m_ready) {
        PlaybackEvent feedback{m_token, Kind::Volume};
        feedback.volumeLinear = linear;
        sendLater(feedback);
    }
}

void AudioEngine::deviceChanged()
{
    log::info(lcMedia, "engine.device_changed",
              {{"playing", m_playing}, {"active", m_active && m_ready && !m_ended}});
    if (!m_active || !m_ready || m_ended) {
        if (m_output->isOpen())
            m_output->open(); // follow the default for the next load
        return;
    }
    const qint64 at = position();
    stopProducer(FlushTail::None); // the device goes
    if (!m_output->open()) {
        log::warn(lcMedia, "engine.no_output");
        return; // no device now; the next change (one appears) picks it up from here
    }
    m_output->setGain(m_volume);
    m_output->setPlaying(m_playing);
    startProducer(at, true);
}
} // namespace cirrus::media
