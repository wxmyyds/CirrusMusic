#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

namespace cirrus::ui
{

class Desktop;
class Player;
class CurrentLyrics;
class AppSettings;
class LyricsDBus;

// MPRIS 2.2 on the session bus (Linux: media keys, the desktop's media controls),
// driven by App.player like the window is, so it behaves as the window does
// (docs/ui/pages/shell.md §7). Raise shows the window, Quit quits. Not created
// when there is no session bus.
class Mpris : public QObject
{
    Q_OBJECT
  public:
    Mpris(Player* player, Desktop* desktop, CurrentLyrics* lyrics = nullptr,
          AppSettings* settings = nullptr, QObject* parent = nullptr);
    ~Mpris() override;

    // Whether the bus name was taken.
    [[nodiscard]] bool registered() const { return m_registered; }

    Player* player() const { return m_player; }
    Desktop* desktop() const { return m_desktop; }
    [[nodiscard]] QString lyricText() const;
    // The player interface's properties now (Position included).
    [[nodiscard]] QVariantMap playerProperties() const;
    [[nodiscard]] QString trackId() const;
    [[nodiscard]] qint64 positionUs() const;

  private:
    void playerChanged();

    Player* m_player;
    Desktop* m_desktop;
    CurrentLyrics* m_lyrics;
    AppSettings* m_settings;
    LyricsDBus* m_lyricsDBus = nullptr;
    bool m_registered = false;
    QVariantMap m_sent; // the player properties last announced
    // For telling a seek from the clock running: the last position base.
    QString m_lastKey;
    qint64 m_lastPositionMs = 0, m_lastPositionAt = 0;
    double m_lastRate = 0;
};

// org.mpris.MediaPlayer2
class MprisRoot : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supported)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supported)

  public:
    explicit MprisRoot(Mpris* owner);
    [[nodiscard]] bool canQuit() const { return true; }
    [[nodiscard]] bool canRaise() const { return true; }
    [[nodiscard]] bool hasTrackList() const { return false; }
    [[nodiscard]] QString identity() const;
    [[nodiscard]] QString desktopEntry() const;
    [[nodiscard]] QStringList supported() const { return {}; }

  public slots:
    void Raise();
    void Quit();

  private:
    Mpris* m_owner;
};

// org.mpris.MediaPlayer2.Player
class MprisPlayer : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

  public:
    explicit MprisPlayer(Mpris* owner);
    [[nodiscard]] QString playbackStatus() const;
    [[nodiscard]] QString loopStatus() const;
    void setLoopStatus(const QString& value);
    [[nodiscard]] double rate() const { return 1.0; }
    void setRate(double value);
    [[nodiscard]] bool shuffle() const;
    void setShuffle(bool value);
    [[nodiscard]] QVariantMap metadata() const;
    [[nodiscard]] double volume() const;
    void setVolume(double value);
    [[nodiscard]] qlonglong position() const;
    [[nodiscard]] bool canGoNext() const;
    [[nodiscard]] bool canGoPrevious() const;
    [[nodiscard]] bool canPlay() const;
    [[nodiscard]] bool canPause() const;
    [[nodiscard]] bool canSeek() const;
    [[nodiscard]] bool canControl() const { return true; }

  public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath& trackId, qlonglong position);
    void OpenUri(const QString&) {}

  signals:
    void Seeked(qlonglong position);

  private:
    Mpris* m_owner;
};

} // namespace cirrus::ui
