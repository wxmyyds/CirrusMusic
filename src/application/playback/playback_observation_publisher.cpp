#include "playback_observation_publisher.h"
#include <QScopedValueRollback>
#include <QSet>
#include <QThread>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace cirrus::application
{
using namespace domain;
namespace
{
qint64 nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
template <class Lyrics> auto tracks(Lyrics& x)
{
    return std::array<decltype(&x.original.standard), 6>{
        &x.original.standard,    &x.original.enhanced,     &x.translation.standard,
        &x.translation.enhanced, &x.romanization.standard, &x.romanization.enhanced};
}
std::array<const LyricCursor*, 6> cursors(const LyricCursors& x)
{
    return {&x.original.standard,    &x.original.enhanced,     &x.translation.standard,
            &x.translation.enhanced, &x.romanization.standard, &x.romanization.enhanced};
}
ObservationContentStatus validate(SongLyrics& x)
{
    qsizetype units = 0;
    const auto add = [&units](const QString& text) {
        if (text.size() > PlaybackObservationPublisher::MaximumTextUnits - units)
            return ObservationContentStatus::LimitExceeded;
        if (!text.isValidUtf16() || text.contains(QChar::Null))
            return ObservationContentStatus::InvalidData;
        units += text.size();
        return ObservationContentStatus::Ready;
    };
    constexpr auto max = ObservedPlayback::MaximumMs;
    for (auto* t : tracks(x)) {
        if (t->lines.size() > 10000)
            return ObservationContentStatus::LimitExceeded;
        if (t->providerOffsetMs < -max || t->providerOffsetMs > max || t->malformedLines < 0 ||
            (t->availability != LyricAvailability::Missing &&
             t->availability != LyricAvailability::Empty &&
             t->availability != LyricAvailability::Present &&
             t->availability != LyricAvailability::Malformed) ||
            (t->timing != LyricTiming::PlainText && t->timing != LyricTiming::Lines &&
             t->timing != LyricTiming::Words) ||
            (t->availability != LyricAvailability::Present && !t->lines.isEmpty()) ||
            (t->availability == LyricAvailability::Present && t->lines.isEmpty()))
            return ObservationContentStatus::InvalidData;
        qsizetype words = 0;
        for (const auto& l : std::as_const(t->lines)) {
            if (l.words.size() > 30000 - words)
                return ObservationContentStatus::LimitExceeded;
            words += l.words.size();
            if ((l.startMs && (*l.startMs < 0 || *l.startMs > max)) ||
                (l.endMs && (*l.endMs < 0 || *l.endMs > max)) ||
                (l.startMs && l.endMs && *l.endMs < *l.startMs))
                return ObservationContentStatus::InvalidData;
            auto result = add(l.text);
            if (result != ObservationContentStatus::Ready)
                return result;
            for (const auto& w : l.words) {
                if (w.startMs < 0 || w.startMs > max || w.durationMs < 0 ||
                    w.durationMs > max - w.startMs)
                    return ObservationContentStatus::InvalidData;
                result = add(w.text);
                if (result != ObservationContentStatus::Ready)
                    return result;
            }
        }
        t->credits.clear(); // source owner unchanged; public v1 omits credits
    }
    return ObservationContentStatus::Ready;
}
ObservationContentStatus validateCursors(const LyricCursors& c, const SongLyrics& l)
{
    const auto cs = cursors(c);
    const auto ts = tracks(l);
    qsizetype count = 0;
    for (size_t i = 0; i < cs.size(); ++i) {
        if (cs[i]->lines.size() > PlaybackObservationPublisher::MaximumCursorEntries - count)
            return ObservationContentStatus::LimitExceeded;
        count += cs[i]->lines.size();
        if (cs[i]->words.size() > PlaybackObservationPublisher::MaximumCursorEntries - count)
            return ObservationContentStatus::LimitExceeded;
        count += cs[i]->words.size();
        QSet<qsizetype> activeLines; // bounded linear lookup, not words × line-count scans
        for (auto index : cs[i]->lines) {
            if (index < 0 || index >= ts[i]->lines.size())
                return ObservationContentStatus::InvalidData;
            activeLines.insert(index);
        }
        for (const auto& w : cs[i]->words)
            if (w.line < 0 || w.line >= ts[i]->lines.size() || w.word < 0 ||
                w.word >= ts[i]->lines[w.line].words.size() || !activeLines.contains(w.line))
                return ObservationContentStatus::InvalidData;
    }
    return ObservationContentStatus::Ready;
}
} // namespace
PlaybackObservationPublisher::PlaybackObservationPublisher(ListeningSession& owner,
                                                           BackendFacade& backend, QObject* parent)
    : QObject(parent), m_owner(&owner), m_backend(&backend), m_clockOrigin(nowMs())
{
    Q_ASSERT(owner.thread() == thread() && backend.thread() == thread());
    m_value.binding = {QUuid::createUuid(), QUuid::createUuid(), {}};
    m_flush.setParent(this);
    m_flush.setSingleShot(true);
    connect(&m_flush, &QTimer::timeout, this, &PlaybackObservationPublisher::capture);
    connect(&owner, &ListeningSession::playbackChanged, this, [this](PlaybackSnapshot s) {
        const bool urgent = s.token != m_seen.token || s.state != m_seen.state ||
                            s.intent != m_seen.intent || s.seekRevision != m_seen.seekRevision ||
                            s.volumeLinear != m_seen.volumeLinear ||
                            s.durationMs != m_seen.durationMs || s.seekable != m_seen.seekable;
        m_seen = s;
        schedule(urgent);
    });
    connect(&owner, &ListeningSession::cursorsChanged, this, [this] { schedule(false); });
    connect(&owner, &ListeningSession::lyricsChanged, this, [this] {
        m_contentDirty = true;
        schedule(true);
    });
    connect(&owner, &ListeningSession::queueChanged, this, [this] { schedule(true); });
    connect(&owner, &ListeningSession::fmChanged, this, [this] { schedule(true); });
    connect(&owner, &ListeningSession::lyricOffsetChanged, this, [this] { schedule(true); });
    connect(&backend, &BackendFacade::sessionChanged, this, [this](SessionSnapshot s) {
        invalidateContext(s); // barrier now, even if phase changes back before queued flush
        schedule(true);
    });
    connect(&backend, &BackendFacade::stateChanged, this, [this] {
        if (m_backend && m_backend->state() != BackendState::Ready)
            invalidateContext({}); // synchronous backend lifecycle barrier
        schedule(true);
    });
    connect(&owner, &QObject::destroyed, this, &PlaybackObservationPublisher::close);
    connect(&backend, &QObject::destroyed, this, &PlaybackObservationPublisher::close);
    schedule(true);
}
bool PlaybackObservationPublisher::contextValid() const
{
    if (m_closed || !m_owner || !m_backend || m_backend->state() != BackendState::Ready)
        return false;
    const auto s = m_backend->snapshot();
    if (!s.token.isValid() ||
        (s.phase != SessionPhase::Guest && s.phase != SessionPhase::Authenticated))
        return false;
    // Empty startup has no content to associate. An external facade load isn't ours.
    const auto ownerLoad = m_owner->playback().token;
    if (!m_backend->playback().token.generation && !ownerLoad.generation)
        return true;
    if (m_retiredRevision && m_owner->intentRevision().first <= *m_retiredRevision)
        return false;
    return ownerLoad != m_retiredLoad && m_backend->playback().token != m_retiredLoad &&
           m_owner->observationSource() == s;
}
bool PlaybackObservationPublisher::cacheCurrent() const
{
    // Revoke an active cached document/cursor as soon as its owner invalidates
    // lyrics, before the queued coherent capture. An empty view has no content
    // to retire and must remain available even while its document is not ready.
    return (!m_contentDirty || !m_value.song) && contextValid() &&
           m_fingerprint == m_backend->snapshot() && m_load == m_owner->playback().token;
}
PlaybackObservation PlaybackObservationPublisher::empty() const
{
    PlaybackObservation value;
    value.binding = {m_value.binding.instanceId, m_value.binding.viewEpoch, {}};
    value.sequence = m_sequence;
    value.sampleMonoMs = nowMs() - m_clockOrigin;
    return value;
}
PlaybackObservation PlaybackObservationPublisher::snapshot() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    // Last coherent sample, not synchronous capture inside partially emitted owner signals.
    return cacheCurrent() ? m_value : empty();
}
std::optional<PlaybackLyricDocument>
PlaybackObservationPublisher::document(ObservationBinding binding, quint64 revision) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!cacheCurrent() || m_contentDirty || !m_document || binding != m_value.binding ||
        revision != m_value.lyricsRevision)
        return {};
    return m_document;
}
std::optional<PlaybackObservationPublisher::ValidatedExport>
PlaybackObservationPublisher::validatedExport() const
{
    if (QThread::currentThread() != thread() || m_retiring || !m_hasSample || !cacheCurrent())
        return {};
    return ValidatedExport{m_value, document(m_value.binding, m_value.lyricsRevision)};
}
ObservationBinding PlaybackObservationPublisher::restartExportInstance()
{
    if (QThread::currentThread() != thread() || m_closed)
        return {};
    QScopedValueRollback<bool> barrier(m_retiring, true);
    retireCurrentView(false);
    if (m_closed)
        return {};
    m_value.binding.instanceId = QUuid::createUuid();
    m_sequence = 0;
    m_clockOrigin = nowMs();
    m_lastSample = -250;
    // A barrier observer may have forced nested capture. It belongs to the OLD
    // instance: do not preserve its load/doc/metadata association or clock evidence.
    m_load = {};
    m_document.reset();
    m_metadata.reset();
    m_metadataStatus = ObservationContentStatus::NotReady;
    m_contentDirty = true;
    m_lyricsRevision = 0;
    m_value = empty();
    m_hasSample = false;
    schedule(true);
    return m_value.binding;
}
void PlaybackObservationPublisher::retireExports()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed)
        return;
    retireCurrentView(true);
    schedule(true);
}
void PlaybackObservationPublisher::schedule(bool urgent)
{
    if (m_closed)
        return;
    if (m_flush.isActive() && !urgent)
        return;
    m_flush.start(urgent ? 0
                         : int(qMax<qint64>(0, 250 - (nowMs() - m_clockOrigin - m_lastSample))));
}
void PlaybackObservationPublisher::close()
{
    if (m_closed)
        return;
    m_closed = true;
    m_flush.stop();
    if (m_sequence != std::numeric_limits<quint64>::max())
        ++m_sequence;
    else {
        m_value.binding.instanceId = QUuid::createUuid(); // retire before counter wrap
        m_sequence = 0;
    }
    retireCurrentView(false);
    emit changed(m_value); // unavailable, never owner commands
}
bool PlaybackObservationPublisher::attachMetadata(ObservationBinding binding, const Song& song)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!cacheCurrent() || binding != m_value.binding || !m_value.song ||
        song.id != *m_value.song || !validMusicId(song.id.value))
        return false;
    qsizetype budget = 0;
    const auto reject = [this](ObservationContentStatus reason) {
        m_metadata.reset();
        m_metadataStatus = reason;
        schedule(true);
        return false;
    };
    if (song.artists.size() > 100)
        return reject(ObservationContentStatus::LimitExceeded);
    const auto text = [&budget, &reject](const QString& s) {
        if (!s.isValidUtf16() || s.contains(QChar::Null))
            return reject(ObservationContentStatus::InvalidData);
        if (s.size() > 4096 || s.size() > 16384 - budget)
            return reject(ObservationContentStatus::LimitExceeded);
        budget += s.size();
        return true;
    };
    PlaybackMetadataView view;
    if (!text(song.title))
        return false;
    view.title = song.title;
    for (const auto& artist : song.artists) {
        if (!text(artist.name))
            return false;
        view.artists.append(artist.name);
    }
    if (song.album) {
        if (!text(song.album->title))
            return false;
        view.album = song.album->title;
    }
    m_metadata = std::move(view); // no cover/IDs/rights or automatic details lookup
    m_metadataStatus = ObservationContentStatus::Ready;
    schedule(true);
    return true;
}
void PlaybackObservationPublisher::invalidateContext(SessionSnapshot fingerprint)
{
    if (fingerprint == m_fingerprint)
        return;
    const bool retireLoad = m_fingerprint.token.isValid();
    m_fingerprint = fingerprint;
    retireCurrentView(retireLoad);
}
void PlaybackObservationPublisher::retireCurrentView(bool retireLoad)
{
    QScopedValueRollback<bool> retiring(m_retiring, true);
    const auto previousBinding = m_value.binding;
    if (retireLoad) {
        // High-water load-intent boundary covers pending/previous tokens across
        // phase-back barriers. Existing local-rejection rollback/equality stays
        // conservatively retired; control-only intents cannot reauthorize.
        if (m_owner)
            m_retiredRevision =
                qMax(m_retiredRevision.value_or(0), m_owner->intentRevision().first);
        // Retire a pending owner intent too, even before any backend position/sample.
        auto previous = m_owner ? m_owner->playback().token : LoadToken{};
        if (!previous.generation)
            previous = m_load.generation ? m_load
                                         : (m_backend ? m_backend->playback().token : LoadToken{});
        if (previous.generation)
            m_retiredLoad = previous;
    }
    m_value.binding.viewEpoch = QUuid::createUuid();
    m_load = {};
    m_document.reset();
    m_metadata.reset();
    m_metadataStatus = ObservationContentStatus::NotReady;
    m_contentDirty = true;
    m_value = empty();
    m_hasSample = false;
    emit contextRetired(previousBinding); // before a queued flush or any reentrant observer
}
void PlaybackObservationPublisher::capture()
{
    if (m_closed || !m_owner || !m_backend)
        return;
    invalidateContext(m_backend->snapshot());
    auto value = empty();
    if (m_sequence == std::numeric_limits<quint64>::max()) {
        close();
        return;
    }
    value.sequence = ++m_sequence;
    if (contextValid()) {
        const auto p = m_owner->playback();
        const bool active = p.state != PlaybackState::Empty;
        if (p.token != m_load) {
            m_load = p.token;
            m_value.binding.trackId = active ? QUuid::createUuid() : QUuid{};
            m_document.reset();
            m_metadata.reset();
            m_metadataStatus = ObservationContentStatus::NotReady;
            m_lyricsRevision = 0;
            m_contentDirty = true;
        }
        value.binding.trackId = active ? m_value.binding.trackId : QUuid{};
        value.status = ObservationStatus::Ready;
        value.state = p.state;
        value.intent = p.intent;
        value.metadata = active ? m_metadata : std::nullopt;
        value.metadataStatus = active ? m_metadataStatus : ObservationContentStatus::NotReady;
        value.mode = m_owner->mode();
        value.fmWaiting = m_owner->waitingForFm();
        value.fmBlocked = m_owner->fmBlocked();
        const auto q = m_owner->queue();
        if (value.mode == ListeningMode::Queue) {
            value.order = q.order;
            value.repeat = q.repeat;
            if (active && !q.active.isNull())
                value.entry = q.active;
        }
        value.capabilities.volume = true; // requestable; observed value still optional
        value.capabilities.modes = value.mode == ListeningMode::Queue;
        value.capabilities.next = m_owner->canGoNext();
        value.capabilities.previous = m_owner->canGoPrevious();
        value.capabilities.play = active || !q.active.isNull();
        value.capabilities.pause =
            active && p.state != PlaybackState::Failed && p.state != PlaybackState::Ended;
        value.capabilities.stop = active;
        value.capabilities.seek = active && p.seekable && p.durationMs.has_value() &&
                                  p.state != PlaybackState::Failed &&
                                  p.state != PlaybackState::Ended;
        if (active) {
            if ((p.state != PlaybackState::Resolving && p.state != PlaybackState::Loading &&
                 p.state != PlaybackState::Ready && p.state != PlaybackState::Playing &&
                 p.state != PlaybackState::Paused && p.state != PlaybackState::Ended &&
                 p.state != PlaybackState::Failed) ||
                (p.intent != TransportIntent::Play && p.intent != TransportIntent::Pause &&
                 p.intent != TransportIntent::Stop) ||
                !validMusicId(p.song.value) || p.positionMs < 0 ||
                p.positionMs > ObservedPlayback::MaximumMs ||
                (p.durationMs &&
                 (*p.durationMs <= 0 || *p.durationMs > ObservedPlayback::MaximumMs ||
                  p.positionMs > *p.durationMs)) ||
                (p.volumeLinear &&
                 (!std::isfinite(*p.volumeLinear) || *p.volumeLinear < 0 || *p.volumeLinear > 1))) {
                m_document.reset();
                m_metadata.reset();
                m_value = empty();
                m_hasSample = true;
                emit changed(m_value);
                return;
            }
            value.song = p.song;
            value.positionMs = p.positionMs;
            value.durationMs = p.durationMs;
            value.preview = p.preview;
            qint64 start = p.preview ? p.preview->startMs : 0;
            if (start < 0 || start > ObservedPlayback::MaximumMs ||
                p.positionMs > ObservedPlayback::MaximumMs - start ||
                (p.preview &&
                 (p.preview->endMs <= start || p.preview->endMs > ObservedPlayback::MaximumMs))) {
                m_value = empty();
                m_document.reset();
                m_metadata.reset();
                m_hasSample = true;
                emit changed(m_value);
                return;
            }
            value.songPositionMs = start + p.positionMs;
            value.positionRevision = p.positionRevision;
            value.seekRevision = p.seekRevision;
            const auto arrival = m_owner->positionObservedAtMs();
            if (arrival && *arrival >= m_clockOrigin)
                value.positionObservedMonoMs = *arrival - m_clockOrigin;
            value.volumeLinear = p.volumeLinear;
            if (p.failure)
                value.failure = p.failure->error;
            value.lyricsState = m_owner->lyricsState();
            value.userOffsetMs = m_owner->lyricOffsetMs();
            if (m_contentDirty) {
                m_document.reset();
                if (m_lyricsRevision == std::numeric_limits<quint64>::max()) {
                    close();
                    return;
                }
                ++m_lyricsRevision;
                m_contentDirty = false;
                if (value.lyricsState == LyricsState::Ready) {
                    auto l = m_owner->lyrics();
                    m_value.documentStatus = !l || l->song != p.song
                                                 ? ObservationContentStatus::InvalidData
                                                 : validate(*l);
                    if (m_value.documentStatus == ObservationContentStatus::Ready)
                        m_document =
                            PlaybackLyricDocument{value.binding, m_lyricsRevision, std::move(*l)};
                } else
                    m_value.documentStatus = ObservationContentStatus::NotReady;
            }
            value.lyricsRevision = m_lyricsRevision;
            value.documentStatus = m_value.documentStatus;
            if (m_document) {
                std::array<ObservationLyricSummary, 6> summaries;
                const auto ts = tracks(std::as_const(m_document->lyrics));
                for (size_t i = 0; i < ts.size(); ++i)
                    summaries[i] = {ts[i]->availability, ts[i]->timing, ts[i]->providerOffsetMs,
                                    ts[i]->lines.size(), ts[i]->malformedLines};
                value.lyricTracks = summaries;
                value.providerNoLyric = m_document->lyrics.providerNoLyric;
                value.instrumental = m_document->lyrics.instrumental;
                auto c = m_owner->cursors();
                value.cursorStatus = validateCursors(c, m_document->lyrics);
                if (value.cursorStatus == ObservationContentStatus::Ready)
                    value.cursors = std::move(c);
            }
        }
    } else {
        m_document.reset();
        m_metadata.reset();
        m_contentDirty = true;
    }
    m_lastSample = value.sampleMonoMs;
    m_value = value;
    m_hasSample = true;
    emit changed(value); // immutable historical sample; reentrant newer intents win next turn
}
} // namespace cirrus::application
