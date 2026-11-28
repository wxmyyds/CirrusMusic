#include "audio_output.h"
#include "automix/mix.h"
#include <QAudioSink>
#include <QMediaDevices>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#ifdef CIRRUS_TEST_AUDIO_THREAD_DIAGNOSTICS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <cstdio>
#include <windows.h>

namespace
{
void logAudioThreadApartment(const char* event)
{
    APTTYPE apartment = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    const HRESULT result = CoGetApartmentType(&apartment, &qualifier);
    std::fprintf(stderr,
                 "CIRRUS_AUDIO_THREAD event=%s tid=%lu apartment_hr=0x%08lx apartment=%d "
                 "qualifier=%d\n",
                 event, static_cast<unsigned long>(GetCurrentThreadId()),
                 static_cast<unsigned long>(result), static_cast<int>(apartment),
                 static_cast<int>(qualifier));
}
} // namespace
#endif

namespace cirrus::media
{
namespace
{
constexpr double kRingSeconds = 0.5;
constexpr double kRampSeconds = 0.008;     // pause/seek fade, no clicks
constexpr float kGainSmoothing = 0.002f;   // per frame towards the volume
constexpr double kTailSeconds = 0.45;      // a skip's fade-out at most (the ring holds 0.5 s)
constexpr double kStartRampSeconds = 0.03; // a track starting under a skip's tail
constexpr qsizetype kTailChunk = 256;
} // namespace

qsizetype AudioRing::write(const float* in, qsizetype frames)
{
    const quint64 at = m_write.load(std::memory_order_relaxed);
    const qsizetype take = std::min(frames, writable());
    for (qsizetype i = 0; i < take; ++i) {
        const size_t slot = size_t((at + quint64(i)) % quint64(m_frames)) * 2;
        m_data[slot] = in[i * 2];
        m_data[slot + 1] = in[i * 2 + 1];
    }
    m_write.store(at + quint64(take), std::memory_order_release);
    return take;
}

qsizetype AudioRing::read(float* out, qsizetype frames)
{
    const quint64 at = m_read.load(std::memory_order_relaxed);
    const qsizetype take = std::min(frames, readable());
    for (qsizetype i = 0; i < take; ++i) {
        const size_t slot = size_t((at + quint64(i)) % quint64(m_frames)) * 2;
        out[i * 2] = m_data[slot];
        out[i * 2 + 1] = m_data[slot + 1];
    }
    m_read.store(at + quint64(take), std::memory_order_release);
    return take;
}

void FadeTail::take(AudioRing& ring, qsizetype frames)
{
    // The rest of the last tail moves to the front at the gain it reached (ascending: each
    // frame is read before anything overwrites it).
    const qsizetype left = m_length - m_at;
    for (qsizetype i = 0; i < left; ++i) {
        const float gain = fade(m_at + i);
        m_data[size_t(i) * 2] = m_data[size_t(m_at + i) * 2] * gain;
        m_data[size_t(i) * 2 + 1] = m_data[size_t(m_at + i) * 2 + 1] * gain;
    }
    frames = std::min(frames, capacity());
    if (frames > left)
        std::fill(m_data.begin() + qsizetype(left) * 2, m_data.begin() + qsizetype(frames) * 2,
                  0.0f);
    float chunk[kTailChunk * 2];
    qsizetype taken = 0;
    while (taken < frames) {
        const qsizetype got = ring.read(chunk, std::min(kTailChunk, frames - taken));
        if (got <= 0)
            break;
        for (qsizetype i = 0; i < got * 2; ++i)
            m_data[size_t(taken) * 2 + size_t(i)] += chunk[i];
        taken += got;
    }
    m_length = std::max(left, taken);
    m_at = 0;
}

float FadeTail::fade(qsizetype at) const
{
    // A quarter cosine from full gain to silence over the whole tail.
    return float(std::cos(double(at) / double(m_length) * std::numbers::pi / 2));
}

void FadeTail::mix(float* data, qsizetype frames, float gain, bool limit)
{
    for (qsizetype i = 0; i < frames && m_at < m_length; ++i, ++m_at) {
        const float g = gain * fade(m_at);
        for (int c = 0; c < 2; ++c) {
            const float sum = data[i * 2 + c] + m_data[size_t(m_at) * 2 + size_t(c)] * g;
            data[i * 2 + c] = limit ? automix::softLimit(sum) : sum;
        }
    }
}

AudioOutput::AudioOutput(QObject* parent) : QObject(parent), m_devices(new QMediaDevices(this))
{
    connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &AudioOutput::devicesChanged);
#ifdef CIRRUS_TEST_AUDIO_THREAD_DIAGNOSTICS
    logAudioThreadApartment("audio-output-created-after-device-connect");
#endif
}

AudioOutput::~AudioOutput()
{
#ifdef CIRRUS_TEST_AUDIO_THREAD_DIAGNOSTICS
    logAudioThreadApartment("audio-output-destroyed");
#endif
    close();
}

bool AudioOutput::open()
{
    close();
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return false;
    QAudioFormat format;
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    format.setSampleRate(device.preferredFormat().sampleRate());
    if (!device.isFormatSupported(format))
        format.setSampleRate(48000);
    if (!device.isFormatSupported(format))
        return false;
    if (!m_ring || format.sampleRate() != m_rate) {
        m_ring = std::make_unique<AudioRing>(qsizetype(format.sampleRate() * kRingSeconds));
        m_tail = FadeTail(qsizetype(format.sampleRate() * kTailSeconds));
    }
    m_rate = format.sampleRate();
    m_device = device;
    m_sink = std::make_unique<QAudioSink>(device, format);
    m_sink->setBufferFrameCount(m_rate / 50); // 20 ms
    m_sink->start([this](QSpan<float> out) { consume(out); });
    return true;
}

void AudioOutput::close()
{
    if (!m_sink)
        return;
    m_sink->stop();
    m_sink.reset();
    m_tail.clear(); // no callback runs now
    m_fading.store(false, std::memory_order_relaxed);
}

qint64 AudioOutput::latencyFrames() const { return m_sink ? m_sink->bufferFrameCount() : 0; }

void AudioOutput::suspend()
{
    if (m_sink && m_sink->state() == QtAudio::ActiveState)
        m_sink->suspend();
}

void AudioOutput::resume()
{
    if (m_sink && m_sink->state() == QtAudio::SuspendedState)
        m_sink->resume();
}

void AudioOutput::beginFlush(FlushTail tail)
{
    // Nothing writes the ring meanwhile (the caller is its producer, or stopped it).
    if (tail != FlushTail::None && m_ring && m_playing.load(std::memory_order_acquire)) {
        const double seconds = tail == FlushTail::Skip ? kTailSeconds : kRampSeconds;
        m_tailWanted.store(std::min(m_ring->readable(), qsizetype(seconds * m_rate)),
                           std::memory_order_release);
    }
    m_flushSeen.store(false, std::memory_order_release);
    m_flush.store(true, std::memory_order_release);
    // The consumer answers within one period; a stopped or suspended sink never does.
    for (int i = 0; i < 100 && !m_flushSeen.load(std::memory_order_acquire); ++i)
        QThread::usleep(500);
    m_tailWanted.store(0, std::memory_order_relaxed); // unseen: a stopped sink plays nothing
    if (m_ring)
        m_ring->drop();
}

void AudioOutput::endFlush()
{
    m_consumed.store(0, std::memory_order_relaxed);
    m_flush.store(false, std::memory_order_release);
}

void AudioOutput::setPlaying(bool playing)
{
    if (playing)
        m_silent.store(false, std::memory_order_relaxed);
    m_playing.store(playing, std::memory_order_release);
}

void AudioOutput::setGain(float linear) { m_target.store(linear, std::memory_order_relaxed); }

void AudioOutput::consume(QSpan<float> out)
{
    float* data = out.data();
    const qsizetype frames = out.size() / 2;
    if (m_flush.load(std::memory_order_acquire) || !m_ring) {
        // The flush's tail (FlushTail): what was queued, kept before the ring is emptied
        // (whoever flushes writes nothing meanwhile).
        if (const qsizetype wanted = m_tailWanted.exchange(0, std::memory_order_acq_rel);
            wanted > 0 && m_ring)
            m_tail.take(*m_ring, wanted);
        m_flushSeen.store(true, std::memory_order_release);
        m_ramp = 0; // fade in after it
        std::memset(data, 0, size_t(out.size()) * sizeof(float));
        m_tail.mix(data, frames, m_gain, false);
        m_fading.store(m_tail.active(), std::memory_order_relaxed);
        return;
    }
    const bool playing = m_playing.load(std::memory_order_acquire);
    if (!playing && m_ramp <= 0) {
        m_silent.store(true, std::memory_order_relaxed);
        std::memset(data, 0, size_t(out.size()) * sizeof(float));
        m_tail.mix(data, frames, m_gain, false);
        m_fading.store(m_tail.active(), std::memory_order_relaxed);
        return;
    }
    // While pausing, keep reading until the fade-out is done.
    const qsizetype got = m_ring->read(data, frames);
    if (got < frames) {
        std::memset(data + got * 2, 0, size_t(frames - got) * 2 * sizeof(float));
        if (playing)
            m_underruns.fetch_add(1, std::memory_order_relaxed);
    }
    const float down = float(1.0 / (kRampSeconds * m_rate));
    const float up = m_tail.active() ? float(1.0 / (kStartRampSeconds * m_rate)) : down;
    const float target = m_target.load(std::memory_order_relaxed);
    for (qsizetype i = 0; i < got; ++i) {
        m_ramp = playing ? std::min(1.f, m_ramp + up) : std::max(0.f, m_ramp - down);
        m_gain += (target - m_gain) * kGainSmoothing;
        const float g = m_ramp * m_gain;
        data[i * 2] *= g;
        data[i * 2 + 1] *= g;
    }
    if (m_tail.active()) {
        m_tail.mix(data, frames, m_gain, true);
        m_fading.store(m_tail.active(), std::memory_order_relaxed);
    }
    m_consumed.fetch_add(quint64(got), std::memory_order_relaxed);
}

void AudioOutput::devicesChanged()
{
    const QAudioDevice now = QMediaDevices::defaultAudioOutput();
    const bool gone = !m_device.isNull() && !QMediaDevices::audioOutputs().contains(m_device);
    if (now.id() == m_device.id() && !gone)
        return;
    emit deviceChanged();
}
} // namespace cirrus::media
