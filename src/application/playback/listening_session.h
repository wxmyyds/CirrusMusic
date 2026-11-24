#pragma once
#include "application/backend/backend_facade.h"
#include "listening_history.h"
#include "lyric_timeline.h"
#include "playback_queue.h"
#include <limits>
#include <utility>

namespace cirrus::application
{
class PersonalFmSource;
// Caller-thread Core orchestration over the ASYNC facade; never owns network/decoder objects.
// Facade must outlive this object. Use this as the sole playback command producer when using a
// queue.
class ListeningSession final : public QObject
{
    Q_OBJECT
  public:
    explicit ListeningSession(BackendFacade& backend, QObject* parent = nullptr);
    ListeningSession(BackendFacade& backend, PersonalFmSource& fm, QObject* parent = nullptr);
    ~ListeningSession() override;
    bool startFm(domain::AudioQuality quality = domain::AudioQuality::Standard,
                 domain::TransportIntent intent = domain::TransportIntent::Play);
    bool retryFm();
    // Plays a Personal FM restored at startup (restoredFm()) on: from `entry`, one of its
    // songs so far, or with a null entry from a new song after them (as next() would).
    bool resumeFm(QUuid entry, domain::AudioQuality quality = domain::AudioQuality::Standard);
    [[nodiscard]] domain::ListeningMode mode() const
    {
        return current() ? m_mode : domain::ListeningMode::Queue;
    }
    [[nodiscard]] bool waitingForFm() const
    {
        return mode() == domain::ListeningMode::PersonalFm && m_fmWaiting;
    }
    [[nodiscard]] bool fmBlocked() const { return waitingForFm() && m_fmBlocked; }
    // `origins`: where each song was picked from (one per song, or none), kept for local
    // statistics. `programs` (one per song, or none): the podcast program a song is the
    // track of; such an entry plays the track, has no lyrics and is recorded as the program.
    bool replaceQueue(const QList<domain::SongId>& songs, qsizetype startIndex = 0,
                      domain::AudioQuality quality = domain::AudioQuality::Standard,
                      const QList<domain::PlayOrigin>& origins = {},
                      const QList<domain::ProgramId>& programs = {});
    // Storage composition only. Never load/play/query during restoration. A Personal FM
    // (`mode`) comes back as its songs so far, shown as queue() until resumeFm() plays it.
    bool restoreQueue(const domain::QueueSnapshot& saved, domain::SessionSnapshot expected,
                      std::pair<quint64, quint64> expectedIntent,
                      domain::ListeningMode mode = domain::ListeningMode::Queue);
    // The restored queue is a Personal FM's (restoreQueue), not played since.
    [[nodiscard]] bool restoredFm() const
    {
        return m_restoredFm && m_mode == domain::ListeningMode::Queue && !current() &&
               queueContextMatches();
    }
    [[nodiscard]] domain::SessionToken queueContext() const { return m_queueContext; }
    bool append(const QList<domain::SongId>& songs, bool playNext = false,
                const domain::PlayOrigin& origin = {},
                const QList<domain::ProgramId>& programs = {});
    bool select(QUuid entry);
    bool remove(QUuid entry);
    // Moves an entry to place `to` of playOrder(); playback goes on.
    bool move(QUuid entry, qsizetype to);
    bool clear();
    bool next();
    bool previous();
    bool pause();
    bool resume();
    bool stop();
    void setBiliSourcePermitted(bool permit);
    bool seek(qint64 clipPositionMs);
    bool setVolume(float linear);
    bool setModes(domain::QueueOrder order, domain::QueueRepeat repeat);
    bool setLyricOffset(qint64 offsetMs);
    void clearHistory();
    void finishHistoryForStorage(); // composition shutdown only; never changes playback
    // Internal provenance/clock seam: NEVER serialize this session fingerprint.
    [[nodiscard]] domain::SessionSnapshot observationSource() const
    {
        return current() ? m_sourceSession : domain::SessionSnapshot{};
    }
    [[nodiscard]] std::optional<qint64> positionObservedAtMs() const
    {
        return current() ? m_positionObservedAt : std::nullopt;
    }
    [[nodiscard]] qint64 lyricOffsetMs() const { return m_userOffset; }
    [[nodiscard]] std::pair<quint64, quint64> intentRevision() const
    {
        return {m_revision, m_controlRevision};
    }
    [[nodiscard]] bool canGoNext() const;
    [[nodiscard]] bool canGoPrevious() const;
    [[nodiscard]] QList<domain::ListeningRecord> history() const
    {
        return m_backend.state() == BackendState::Ready &&
                       m_history.context() == m_backend.snapshot().token
                   ? m_history.records()
                   : QList<domain::ListeningRecord>{};
    }
    [[nodiscard]] domain::QueueSnapshot queue() const
    {
        return m_mode == domain::ListeningMode::Queue &&
                       (!m_queueContext.isValid() || queueContextMatches())
                   ? m_queue.snapshot()
                   : domain::QueueSnapshot{};
    }
    // Personal FM's songs so far, oldest first, the one playing (or the last one, while the
    // next is fetched) active; read on fmChanged. Empty outside Personal FM.
    [[nodiscard]] domain::QueueSnapshot fmQueue() const
    {
        return mode() == domain::ListeningMode::PersonalFm ? m_queue.snapshot()
                                                           : domain::QueueSnapshot{};
    }
    // queue()'s entry ids in play order; read on queueChanged. Empty when queue() is.
    [[nodiscard]] QList<QUuid> playOrder() const
    {
        return m_mode == domain::ListeningMode::Queue &&
                       (!m_queueContext.isValid() || queueContextMatches())
                   ? m_queue.playOrder()
                   : QList<QUuid>{};
    }
    [[nodiscard]] domain::PlaybackSnapshot playback() const
    {
        return current() ? m_playback : domain::PlaybackSnapshot{};
    }
    [[nodiscard]] domain::LyricsState lyricsState() const
    {
        return current() ? m_lyricsState : domain::LyricsState::None;
    }
    [[nodiscard]] std::optional<domain::SongLyrics> lyrics() const
    {
        return current() ? m_lyrics : std::nullopt;
    }
    [[nodiscard]] domain::LyricCursors cursors() const
    {
        return current() ? m_cursors : domain::LyricCursors{};
    }
  signals:
    void historyChanged(); // notification only, owning-thread getter; never a reporting event
    void
    listeningRecordChanged(cirrus::domain::StoredListeningRecord record); // private local store
    void reportingRecordChanged(cirrus::domain::StoredListeningRecord record,
                                cirrus::domain::SourceProvider provider);
    void failureStreakExceeded();
    void historyCleared(cirrus::domain::SessionSnapshot source);
    void historyStorageOverflow(); // explicit gap, not silent durable retention
    void fmChanged();              // notification only; read current mode/waiting/blocked getters
    void queueChanged(cirrus::domain::QueueSnapshot queue);
    void queueCleared(cirrus::domain::SessionSnapshot source); // explicit user clear, not reset
    void playbackChanged(cirrus::domain::PlaybackSnapshot snapshot);
    void lyricsChanged(cirrus::domain::LyricsState state);
    void lyricsFailed(cirrus::application::MusicFailure failure);
    void cursorsChanged(cirrus::domain::LyricCursors cursors);
    void lyricOffsetChanged(qint64 offsetMs);
    void
    advanceStopped(bool failed); // false=end of queue; true=bounded failure/busy, no retry loop
  private:
    bool begin(PlaybackQueue candidate, domain::TransportIntent intent,
               domain::AudioQuality quality, bool automatic, bool fm = false);
    // Takes `candidate` as the queue playing m_backend.playback() (accepted load).
    void commit(PlaybackQueue candidate, domain::TransportIntent intent,
                domain::AudioQuality quality, bool automatic, bool fm,
                domain::SessionSnapshot sourceSession, quint64 revision);
    // The next song is handed to the backend ahead of time so it follows without a gap
    // (docs/tasks/audio-engine.md §4.5); redone when the queue changes.
    void refreshPreload();
    // Personal FM's: the next of its songs so far after going back, else the supply's
    // first, looked at and taken only once it plays (docs/tasks/automix.md §7).
    void refreshFmPreload();
    [[nodiscard]] PlaybackQueue fmQueueWith(const domain::SongId& song) const;
    void requestPreload(domain::SongId song, QUuid entry, PlaybackQueue queue, bool fromFm);
    void adoptAdvance();
    bool enterFmWait(bool automatic, bool stopMedia);
    void scheduleFm();
    void consumeFm();
    void blockFm();
    void clearFmState();
    // The Personal FM song `step` places from the active one in fmQueue(), or null.
    [[nodiscard]] QUuid fmNeighbour(int step) const;
    bool playFm(QUuid entry, bool automatic);
    bool acceptLoadCommand(quint64 id, quint64 revision);
    bool stopPlayback(bool keepFmQueue);
    void receive(domain::PlaybackSnapshot snapshot);
    void advance(bool naturalEnd);
    void reset();
    void cancelLyrics();
    void requestLyrics();
    void sourceSelected(domain::PlaybackSourceSelection selection);
    void refreshCursors();
    bool notifyHistory(bool notifyView = true);
    bool current() const;
    bool queueContextMatches() const { return m_queueContext == m_backend.snapshot().token; }
    void recordControlIntent()
    {
        if (m_controlRevision != std::numeric_limits<quint64>::max())
            ++m_controlRevision; // saturate: the native bridge refuses exhausted epochs
    }
    BackendFacade& m_backend;
    PersonalFmSource* m_fm = nullptr; // injected source and facade must outlive this session
    domain::ListeningMode m_mode = domain::ListeningMode::Queue;
    std::optional<domain::Song> m_fmPending;
    quint64 m_fmScheduledRevision = 0;
    int m_failureStreak = 0;
    qint64 m_lastObservedPlayedMs = 0;
    bool m_heard = false;
    bool m_lyricsRequested = false;
    bool m_permitBiliSource = true;
    bool m_failureStopNotified = false;
    std::optional<quint64> m_pendingFailureStop;
    void stopForFailures(quint64 revision);
    bool m_fmWaiting = false, m_fmBlocked = false;
    bool m_restoredFm = false; // the restored queue is a Personal FM's
    PlaybackQueue m_queue;
    ListeningHistory m_history;
    LyricTimeline m_timeline;
    domain::PlaybackSnapshot m_playback;
    domain::LoadToken m_token;
    domain::SessionToken m_queueContext;
    domain::SessionSnapshot m_sourceSession;
    std::optional<qint64> m_positionObservedAt;
    domain::AudioQuality m_quality{};
    domain::TransportIntent m_intent{};
    domain::LyricsState m_lyricsState{};
    std::optional<domain::SongLyrics> m_lyrics;
    domain::LyricCursors m_cursors;
    qint64 m_userOffset = 0;
    quint64 m_lyricRequest = 0, m_revision = 0, m_controlRevision = 0;
    quint64 m_preloadLyricRequest = 0; // the preloaded song's: its sung lines, for transitions
    quint64 m_historyClearEpoch = 0;
    QSet<QUuid> m_autoVisited;
    struct Preload {
        quint64 generation = 0; // the load it plays under
        QUuid entry;
        domain::SongId song;
        PlaybackQueue queue; // the queue once it plays
        bool fromFm = false; // Personal FM's supply's next: taken from it when it plays
    };
    std::optional<Preload> m_preload;
    bool m_preloadChecked = false; // refreshPreload() ran for this load
    bool m_starting = false, m_auto = false, m_terminalHandled = false;
};
} // namespace cirrus::application
