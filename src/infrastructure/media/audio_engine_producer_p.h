#pragma once
#include "audio_engine.h"
#include "audio_decoder.h"
#include "audio_output.h"
#include "audio_store.h"
#include "automix/deck.h"
#include "automix/mix.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace cirrus::media
{
// Private producer state, shared only at the explicitly locked owner/decoder boundary.
struct AudioEngine::Producer : std::enable_shared_from_this<AudioEngine::Producer> {
    // Private owner/producer boundary. Next fields through replan are locked; deck
    // state below is decoder-thread-only. `crossed` means decoding has crossed; the
    // owner publishes Advanced when the sink reaches `boundary` (or on fatal incoming
    // failure). It must not be used as the identity of either overlapping decoder.
    std::mutex lock;
    std::condition_variable wake;
    bool quit = false;
    std::optional<qint64> seekTo, seeking;
    qint64 baseMs = 0;
    quint64 startFrame = 0, seekSerial = 0;
    std::shared_ptr<AudioStream> nextStream;
    domain::LoadToken nextToken;
    QString nextFormat;
    float nextGain = 1;
    bool nextPreview = false, nextSameFile = false;
    bool crossed = false, nextFailed = false;
    quint64 boundary = 0, nextStartFrame = 0;
    qint64 nextBaseMs = 0, nextDurationMs = 0;
    automix::Settings transition;
    std::shared_ptr<const automix::Analysis> curAnalysis, nextAnalysis;
    std::vector<automix::Span> curVocals, nextVocals;
    bool replan = false;
    std::atomic<quint64> produced{0};
    std::atomic<bool> eof{false};
    AudioOutput* output = nullptr;
    int outRate = 0;
    std::shared_ptr<AudioStream> stream; // initial stream; owner joins before releasing output
    QString format;
    bool preview = false;
    float trackGain = 1;
    std::atomic<bool> normalize{false};
    std::thread thread;

    [[nodiscard]] bool quitting()
    {
        std::lock_guard guard(lock);
        return quit;
    }
    [[nodiscard]] bool undisturbed()
    {
        std::lock_guard guard(lock);
        return !quit && !seekTo;
    }
    void run(AudioEngine* engine, domain::LoadToken token, bool reopening);

  private:
    friend class AudioEngineFailureTest;
    struct Upcoming {
        std::shared_ptr<AudioStream> stream;
        std::unique_ptr<AudioDecoder> decoder;
        domain::LoadToken token;
        float gain = 1;
        bool preview = false;
        automix::Plan plan;
    };
    struct Mixing {
        std::unique_ptr<automix::Deck> deck;
        domain::LoadToken token;
        automix::Plan plan;
        automix::Mixer mixer;
        qint64 durationMs = 0;
        quint64 startFrame = 0;
        qint64 offset = 0, length = 0;
    };
    void failed(AudioEngine* engine, domain::LoadToken token, domain::PlaybackError error);
    qsizetype pullFrom(automix::Deck& deck, float* out, qsizetype frames);
    void prepare(AudioEngine* engine);
    void planAgain(AudioEngine* engine);
    bool startMixing();
    bool mixSome(AudioEngine* engine, std::vector<float>& out);
    bool crossNext();
    bool crossLate();
    domain::LoadToken m_curToken; // follows the actual decoder, independently of Advanced
    std::unique_ptr<automix::Deck> m_cur;
    qint64 m_curDurationMs = 0;
    quint64 m_curOrigin = 0;
    std::optional<Upcoming> m_upcoming;
    std::optional<Mixing> m_mixing;
    bool m_outgoingFailed = false;
    std::vector<float> m_other;
};
} // namespace cirrus::media
