#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

class AppSettings;
class CurrentLyrics;
class Player;

// Lets other programs of this user read what is playing and its lyrics (the read-only local
// interface of docs/tasks/B-031-playback-observation.md; protocol in tools/now-playing/README.md):
// on while the setting is, Linux only. Adds what the backend's view does not know itself:
// the song's title, artists and album, and the user's lyric offset.
class NowPlayingExport : public QObject
{
    Q_OBJECT

  public:
    explicit NowPlayingExport(runtime::BackendRuntime& runtime, QObject* parent = nullptr);
    ~NowPlayingExport() override;

    // Whether this system has the interface (Linux).
    [[nodiscard]] static bool available();

    // Follows the setting, the current song and its lyric offset.
    void follow(AppSettings* settings, Player* player, CurrentLyrics* lyrics);
    void setEnabled(bool enabled);
    // Stops it; `done` runs once its socket and files are gone (at most about 2 s).
    void close(std::function<void()> done);

  signals:
    // Turning it on failed (no private runtime directory, say).
    void failed();

  private:
    struct Song {
        QString key, title, album;
        QStringList artists;
        friend bool operator==(const Song&, const Song&) = default;
    };
    void attach();
    void stop();

    struct Service;
    runtime::BackendRuntime& m_runtime;
    std::unique_ptr<Service> m_service;
    std::optional<Song> m_song;
    qint64 m_offsetMs = 0;
    bool m_closed = false;
};

} // namespace cirrus::ui
