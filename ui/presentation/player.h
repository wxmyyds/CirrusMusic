#pragma once

#include "playback.h"
#include "song_item.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

Q_MOC_INCLUDE("song_list_model.h")

namespace cirrus::ui
{

class AppSettings;
class PlaybackSource;
class SongDetailLoader;
class SongListModel;

// Current playback (App.player, docs/ui/presentation.md §5.1): the playback source's
// snapshot for QML, and its commands. Snapshots arrive only when something changes,
// not every frame; smooth positions come from PlaybackClock in QML.
class Player : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.player")
    Q_PROPERTY(cirrus::ui::SongItem current READ current NOTIFY currentChanged)
    Q_PROPERTY(int biliStatus READ biliStatus NOTIFY biliAccountChanged)
    Q_PROPERTY(QString biliAccountName READ biliAccountName NOTIFY biliAccountChanged)
    Q_PROPERTY(quint64 generation READ generation NOTIFY changed)
    Q_PROPERTY(int provider READ provider NOTIFY changed)
    Q_PROPERTY(int status READ status NOTIFY changed) // Playback.Status
    // Translated reason when Failed, else empty.
    Q_PROPERTY(QString failureMessage READ failureMessage NOTIFY changed)
    // Position base: `positionMs` at `positionAt` (now()), advancing by `rate`.
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY changed)
    Q_PROPERTY(qint64 positionAt READ positionAt NOTIFY changed)
    Q_PROPERTY(double rate READ rate NOTIFY changed)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY changed) // -1 when unknown
    Q_PROPERTY(bool seekable READ seekable NOTIFY changed)
    // Only a preview clip plays, starting this far into the song; -1 for the whole song.
    Q_PROPERTY(qint64 previewStartMs READ previewStartMs NOTIFY changed)
    Q_PROPERTY(double volume READ volume NOTIFY changed)
    Q_PROPERTY(int order READ order NOTIFY changed)   // Playback.Order
    Q_PROPERTY(int repeat READ repeat NOTIFY changed) // Playback.Repeat
    Q_PROPERTY(bool canNext READ canNext NOTIFY changed)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY changed)
    Q_PROPERTY(int mode READ mode NOTIFY changed) // Playback.Mode
    Q_PROPERTY(bool fmWaiting READ fmWaiting NOTIFY changed)
    // Whether podcast voices can be played (playVoices()).
    Q_PROPERTY(bool canPlayVoices READ canPlayVoices CONSTANT)
    // The queue in play order; rows get details as views ask (ensureQueueLoaded).
    // A song can be in it twice: the current row is `queueIndex`, not the current role.
    Q_PROPERTY(cirrus::ui::SongListModel* queue READ queue CONSTANT)
    Q_PROPERTY(int queueIndex READ queueIndex NOTIFY changed)
    // 心动模式 (docs/tasks/heartbeat-mode.md §7): the queue is a heartbeat mix of the liked
    // songs; one is being fetched; it can start (signed in, the playing song from the liked
    // songs) or is on. `queueRecommended`: per row of `queue`, a song the mix put in.
    Q_PROPERTY(bool heartbeat READ heartbeat NOTIFY changed)
    Q_PROPERTY(bool heartbeatLoading READ heartbeatLoading NOTIFY changed)
    Q_PROPERTY(bool heartbeatAvailable READ heartbeatAvailable NOTIFY changed)
    Q_PROPERTY(QList<bool> queueRecommended READ queueRecommended NOTIFY changed)
    // The one play-mode button's (PlayerControls, MiniPlayer), cycling as NetEase does:
    // 0 list repeat, 1 repeat one, 2 shuffle, 3 in order (stops at the end), 4 心动模式
    // (in the cycle only while heartbeatAvailable).
    Q_PROPERTY(int playMode READ playMode NOTIFY changed)
    // The cover of the song after this one, when known: views fetch it ahead.
    Q_PROPERTY(QString upcomingCoverKey READ upcomingCoverKey NOTIFY upcomingChanged)

  public:
    // Without a source (no playback in this build) the player stays Empty. `settings`
    // (may be null) keeps the volume, order and repeat, applied at once.
    explicit Player(PlaybackSource* source, AppSettings* settings = nullptr,
                    QObject* parent = nullptr);

    [[nodiscard]] SongItem current() const { return m_current; }
    [[nodiscard]] int status() const { return m_snapshot.status; }
    [[nodiscard]] int provider() const { return m_snapshot.provider; }
    [[nodiscard]] quint64 generation() const { return m_snapshot.generation; }
    [[nodiscard]] int biliStatus() const;
    [[nodiscard]] QString biliAccountName() const;
    Q_INVOKABLE void saveBiliCookie(const QString& cookie);
    Q_INVOKABLE void clearBiliCookie();
    [[nodiscard]] QString failureMessage() const;
    [[nodiscard]] qint64 positionMs() const { return m_snapshot.positionMs; }
    [[nodiscard]] qint64 positionAt() const { return m_snapshot.positionAt; }
    [[nodiscard]] double rate() const { return m_snapshot.rate; }
    [[nodiscard]] qint64 durationMs() const { return m_snapshot.durationMs; }
    [[nodiscard]] bool seekable() const { return m_snapshot.seekable; }
    [[nodiscard]] qint64 previewStartMs() const { return m_snapshot.previewStartMs; }
    [[nodiscard]] double volume() const { return m_snapshot.volume; }
    [[nodiscard]] int order() const { return m_snapshot.order; }
    [[nodiscard]] int repeat() const { return m_snapshot.repeat; }
    [[nodiscard]] bool canNext() const { return m_snapshot.canNext; }
    [[nodiscard]] bool canPrevious() const { return m_snapshot.canPrevious; }
    [[nodiscard]] int mode() const { return m_snapshot.mode; }
    [[nodiscard]] bool fmWaiting() const { return m_snapshot.fmWaiting; }
    [[nodiscard]] SongListModel* queue() const { return m_queue; }
    [[nodiscard]] int queueIndex() const { return m_snapshot.queueIndex; }
    [[nodiscard]] QString upcomingCoverKey() const { return m_upcomingCoverKey; }
    [[nodiscard]] bool heartbeat() const { return m_snapshot.heartbeat; }
    [[nodiscard]] bool heartbeatLoading() const { return m_snapshot.heartbeatLoading; }
    [[nodiscard]] bool heartbeatAvailable() const;
    [[nodiscard]] QList<bool> queueRecommended() const { return m_snapshot.recommended; }
    [[nodiscard]] int playMode() const;
    // The account's liked songs (Library::likedPlaylistKey): whose mix 心动模式 is.
    void setLikedPlaylist(const QString& key);

    // The clock of `positionAt`, in ms.
    Q_INVOKABLE static double now() { return double(playback::now()); }

    // `origin` (Playback.Origin), `originId` and `originName`: where the songs come from,
    // e.g. playSongs(keys, 0, Playback.FromAlbum, albumKey, albumTitle).
    Q_INVOKABLE void playSongs(const QStringList& keys, int index, int origin = 0,
                               const QString& originId = QString(),
                               const QString& originName = QString());
    Q_INVOKABLE void playVoices(const QStringList& keys, int index);
    [[nodiscard]] bool canPlayVoices() const;
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 positionMs);
    // Plays row `index` of `queue`.
    Q_INVOKABLE void playAt(int index);
    // Edits the queue (mode Queue): the rows change at once, the source follows.
    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void moveQueue(int from, int to);
    // Adds `keys` right after the playing song (`next`) or at the end, as playSongs()
    // takes them; with an empty queue they play. Personal FM refuses (a notice says why).
    Q_INVOKABLE void addSongs(const QStringList& keys, bool next, int origin = 0,
                              const QString& originId = QString(),
                              const QString& originName = QString());
    // Plays `key` (a song, or a voice the catalog read) right after the playing one, keeping
    // the queue (PlaybackSource::playNow).
    Q_INVOKABLE void playNow(const QString& key, bool voice = false);
    Q_INVOKABLE void clearQueue();
    // Details for queue rows [first, last] (clamped), e.g. the rows on screen.
    Q_INVOKABLE void ensureQueueLoaded(int first, int last);
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void setOrder(int order);
    Q_INVOKABLE void setRepeat(int repeat);
    Q_INVOKABLE void startFm();
    Q_INVOKABLE void setPlayMode(int mode);
    // To the next play mode (playMode).
    Q_INVOKABLE void cyclePlayMode();
    // 心动模式 from the playing song (heartbeatAvailable); setOrder()/setRepeat() leave it.
    Q_INVOKABLE void startHeartbeat();
    // Out of 心动模式 (or a mix being fetched), back to the modes from before it.
    Q_INVOKABLE void stopHeartbeat();
    // 心动模式 over the liked songs `keys` (as their page shows them) from their start.
    Q_INVOKABLE void startHeartbeatWith(const QStringList& keys, const QString& title);

  signals:
    void currentChanged();
    void biliAccountChanged();
    void changed();
    void upcomingChanged();
    // A short message for a snackbar (only a preview clip plays).
    void notice(const QString& message);

  private:
    void update();
    // The current song's details: the source's, else its queue row's.
    void updateCurrent();
    void updateUpcoming();
    void noticePreview();

    // The source's order and repeat back to the settings' (a restored queue has its own).
    void applyModes();

    PlaybackSource* m_source;
    AppSettings* m_settings;
    PlaybackSnapshot m_snapshot;
    SongItem m_current;
    QString m_upcomingCoverKey;
    // The last song whose playing showed whether it is a preview, and whether it was.
    QString m_previewKnownKey;
    bool m_previewKnown = false;
    SongListModel* m_queue;
    SongDetailLoader* m_queueDetails;
    QString m_likedPlaylist;
    bool m_leavingHeartbeat = false; // a mode change leaves the mix: say so once it has
};

} // namespace cirrus::ui
