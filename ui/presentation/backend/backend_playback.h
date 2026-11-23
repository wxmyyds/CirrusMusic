#pragma once

#include "sources.h"

#include "domain/playback/playback_types.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUuid>

namespace cirrus::application
{
class BackendFacade;
class ListeningSession;
} // namespace cirrus::application
namespace cirrus::cache
{
class CachedMusicQueryService;
struct CachedMusicResult;
} // namespace cirrus::cache
namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

class MusicRequests;

// Playback through the backend's ListeningSession (docs/ui/integration.md §4
// PlaybackSource), the only one that sends playback commands. The snapshot is made
// from the session's queue and playback; the current song's details come from
// `catalog`, which must outlive this. Positions only notify when they leave the
// clock's extrapolation.
class BackendPlayback : public QObject, public PlaybackSource
{
  public:
    BackendPlayback(runtime::BackendRuntime& runtime, CatalogSource& catalog,
                    QObject* parent = nullptr);
    ~BackendPlayback() override;

    [[nodiscard]] const PlaybackSnapshot& snapshot() const override { return m_state; }
    void setChangedHandler(std::function<void()> changed) override
    {
        m_changed = std::move(changed);
    }

    void setNoticeHandler(std::function<void(const QString&)> notice) override
    {
        m_notice = std::move(notice);
    }
    void playSongs(const QStringList& keys, int index, const PlayOrigin& origin = {}) override;
    // Where a queue entry's song came from (the listening reports); remembered per
    // account with the queue, so a restored queue keeps it.
    [[nodiscard]] PlayOrigin originOf(QUuid entry);
    // Podcast voices the catalog or library read (KnownVoices): each plays its track and
    // is shown, recorded and reported as the voice (docs/api/podcast-playback.md). Those
    // without a track are left out.
    void playVoices(const QStringList& keys, int index) override;
    [[nodiscard]] bool canPlayVoices() const override { return true; }
    void playPause() override;
    void next() override;
    void previous() override;
    void seek(qint64 positionMs) override;
    void playAt(int index) override;
    void removeAt(int index) override;
    void move(int from, int to) override;
    bool addSongs(const QStringList& keys, bool next, const PlayOrigin& origin = {}) override;
    void clearQueue() override;
    void setVolume(double volume) override;
    void setNormalization(bool on) override;
    void setBiliSourceEnabled(bool on) override;
    [[nodiscard]] BiliAccountState biliAccount() const override;
    void setBiliAccountHandler(std::function<void()> changed) override
    {
        m_biliChanged = std::move(changed);
    }
    void saveBiliCookie(const QString& cookie) override;
    void clearBiliCookie() override;
    void setTransition(int mode, int crossfadeSeconds) override;
    void setOrder(playback::Order order) override;
    void setRepeat(playback::Repeat repeat) override;
    void startFm() override;

  private:
    // Rebuilds the snapshot; notifies on the next turn when it changed.
    void update();
    void notify();
    // The entry id at `index` of the snapshot's queue, null outside it.
    [[nodiscard]] QUuid entryAt(int index) const;
    void fetchDetails(const QString& key);
    // The song's details when known (asked for when not), else just its key.
    SongInfo songInfo(const QString& key);
    // What played last, while nothing plays (keep(), showKept()); replay() plays it again.
    void keep(qint64 now);
    void showKept(PlaybackSnapshot& next, qint64 now);
    // The queue restored at startup, before anything played; playRestored() plays it on.
    void showRestored(PlaybackSnapshot& next, qint64 now);
    void playRestored();
    // A Personal FM restored at startup plays on from the song `step` places from its
    // current one (past the last: a new one); false when the queue is not one.
    bool resumeRestoredFm(int step);
    // Where the queue is, per account, for the next start (`stopped`: now, not throttled).
    void savePosition(const PlaybackSnapshot& state, qint64 now, bool stopped);
    struct Saved {
        qint64 songMs = 0; // on the song's clock
        qint64 durationMs = -1;
        qint64 previewStartMs = -1;
    };
    [[nodiscard]] std::optional<Saved> saved(const QString& songKey) const;
    [[nodiscard]] bool showingKept() const;
    void replay(int index);
    // After replay(): to where it was, once the song can seek.
    void seekWhereKept(const QString& key, const domain::PlaybackSnapshot& play);
    // Signed in or a guest, not in the middle of a change.
    [[nodiscard]] bool settled() const;
    [[nodiscard]] bool canResume() const;
    [[nodiscard]] int stepped(int step) const;

    // What played last, so the mini player never goes away once something played: a
    // session change (signing in or out) retires the backend's queue and stops it, and
    // the queue can be emptied.
    struct Kept {
        QStringList keys; // play order; empty when the queue was emptied
        int index = -1;
        SongInfo current;
        playback::Provider provider = playback::Netease;
        qint64 songPositionMs = 0; // on the song's clock (a preview's start added)
        qint64 durationMs = -1;
        qint64 previewStartMs = -1;
        bool resume = false;       // playing when a session change stopped it
        QList<PlayOrigin> origins; // per key
        QList<bool> voices;        // per key: a podcast voice
    };
    void applyVolume();
    void applyNormalization();
    void applyTransition();
    // `voices` (per key, or none): which keys are podcast voices (in m_voices or
    // KnownVoices).
    void startQueue(const QStringList& keys, int index, const QList<PlayOrigin>& origins,
                    const QList<bool>& voices = {});
    // The details of the voices in the queue, per account, for the next start.
    void loadVoices();
    void saveVoices();
    void loadOrigins();
    void saveOrigins() const;

    runtime::BackendRuntime& m_runtime;
    QPointer<application::ListeningSession> m_session;
    std::function<void(const QString&)> m_notice;
    std::function<void()> m_biliChanged;
    QPointer<application::BackendFacade> m_facade;
    CatalogSource& m_catalog;
    QHash<QUuid, PlayOrigin> m_origins;
    // Details of the podcast voices in the queue (by voice key), with their tracks.
    QHash<QString, SongInfo> m_voices;
    QString m_voicesAccount; // whose m_voices are
    bool m_voicesLoaded = false;
    QList<bool> m_queueVoices; // per key of the snapshot's queue: a voice
    QString m_originsAccount;  // whose m_origins are
    bool m_originsLoaded = false;
    std::function<void()> m_changed;
    PlaybackSnapshot m_state;
    QList<QUuid> m_entries; // the snapshot's queue as entry ids (Queue mode)
    domain::LoadToken m_load;
    quint64 m_positionRevision = 0;
    // Details of songs that played, by key; reset when it grows large.
    QHash<QString, SongInfo> m_details;
    QString m_detailsKey; // asked for
    Request m_detailsRequest;
    bool m_detailsArrived = false;
    std::optional<double> m_volume;  // the user's, once set
    domain::LoadToken m_volumeFixed; // the load whose volume was set again
    std::optional<bool> m_normalize; // the setting, once known
    struct Transition {
        domain::TransitionMode mode;
        qint64 crossfadeMs;
    };
    std::optional<Transition> m_transition; // the setting, once known
    bool m_clearing = false;                // clearQueue() is stopping playback
    bool m_notifyPending = false;
    bool m_live = false;       // something loaded (or Personal FM waiting)
    bool m_removing = false;   // in removeAt(): a stop now is the user's
    bool m_started = false;    // something played or a queue came back (startup is over)
    QString m_positionAccount; // whose position savePosition() writes
    QString m_savedKey;
    playback::Status m_savedStatus = playback::Empty;
    qint64 m_savedAt = 0;
    QTimer m_resume;
    std::optional<Kept> m_kept;
    QString m_seekKey; // replay()'s song and where on its clock
    qint64 m_seekSongMs = 0;
};

// Playback lyrics come from the selected load's session document; Bilibili subtitle
// absence may resolve to NetEase lyrics there. Non-playback songs use the resource cache.
class BackendLyrics : public QObject, public LyricsSource
{
  public:
    explicit BackendLyrics(runtime::BackendRuntime& runtime, QObject* parent = nullptr);
    ~BackendLyrics() override;

    Request lyrics(const QString& songKey, Reply<SongLyrics> reply) override;

  private:
    struct Job;
    void sessionLyrics();
    bool routePlayback(const std::shared_ptr<Job>& job);
    // The cache first, then the service.
    void start(const std::shared_ptr<Job>& job);
    void completed(const cache::CachedMusicResult& result);
    void fromService(const std::shared_ptr<Job>& job);

    QPointer<application::ListeningSession> m_session;
    QPointer<cache::CachedMusicQueryService> m_queries;
    MusicRequests* m_requests;
    QPointer<application::BackendFacade> m_facade;
    QList<std::shared_ptr<Job>> m_waiting;          // for the session's
    QHash<quint64, std::shared_ptr<Job>> m_pending; // by cache query id
};

} // namespace cirrus::ui
