#pragma once

#include "sources.h"

#include <QObject>
#include <QQuickAsyncImageProvider>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace cirrus::ui
{

class AppSettings;
class CacheStatus;
class Diagnostics;
class FakeControls;
class Formatter;
class CurrentLyrics;
class Library;
class LocalStats;
class NetworkStatus;
class UpdateCheck;
class Player;
class Session;
class Share;
class ShortcutMap;
class Together;

// Root of the presentation layer for QML (`import Cirrus.Presentation`, then `App.…`).
// main.cpp installs the data sources before loading QML:
//   cirrus::ui::App::install(cirrus::ui::makeFakeSources());
//   engine.addImageProvider("cover", cirrus::ui::App::instance()->makeCoverProvider());
// Page ViewModels take their sources from here. All on the GUI thread.
class App : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("app_settings.h")
    Q_MOC_INCLUDE("cache_status.h")
    Q_MOC_INCLUDE("diagnostics.h")
    Q_MOC_INCLUDE("fake_controls.h")
    Q_MOC_INCLUDE("formatter.h")
    Q_MOC_INCLUDE("lyrics.h")
    Q_MOC_INCLUDE("library.h")
    Q_MOC_INCLUDE("stats_model.h")
    Q_MOC_INCLUDE("network_status.h")
    Q_MOC_INCLUDE("player.h")
    Q_MOC_INCLUDE("update_check.h")
    Q_MOC_INCLUDE("session.h")
    Q_MOC_INCLUDE("together.h")
    Q_MOC_INCLUDE("sharing.h")
    Q_MOC_INCLUDE("shortcut_map.h")
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(cirrus::ui::AppSettings* settings READ settings CONSTANT)
    Q_PROPERTY(cirrus::ui::CacheStatus* cache READ cache CONSTANT)
    Q_PROPERTY(cirrus::ui::Diagnostics* diagnostics READ diagnostics CONSTANT)
    Q_PROPERTY(cirrus::ui::Formatter* format READ format CONSTANT)
    Q_PROPERTY(cirrus::ui::Library* library READ library CONSTANT)
    Q_PROPERTY(cirrus::ui::CurrentLyrics* lyrics READ lyrics CONSTANT)
    Q_PROPERTY(cirrus::ui::NetworkStatus* network READ network CONSTANT)
    Q_PROPERTY(cirrus::ui::Player* player READ player CONSTANT)
    Q_PROPERTY(cirrus::ui::Session* session READ session CONSTANT)
    Q_PROPERTY(cirrus::ui::LocalStats* stats READ stats CONSTANT)
    Q_PROPERTY(cirrus::ui::UpdateCheck* updates READ updates CONSTANT)
    Q_PROPERTY(cirrus::ui::Together* together READ together CONSTANT)
    Q_PROPERTY(cirrus::ui::Share* share READ share CONSTANT)
    // The keyboard shortcuts the user can change (docs/ui/pages/shortcuts.md).
    Q_PROPERTY(cirrus::ui::ShortcutMap* shortcuts READ shortcuts CONSTANT)

    // Fake source knobs; null with real sources.
    Q_PROPERTY(cirrus::ui::FakeControls* debug READ debug CONSTANT)

  public:
    // What an opened link is (applink::Target, docs/ui/pages/open-links.md).
    enum LinkTarget {
        LinkSong,
        LinkVoice,
        LinkPlaylist,
        LinkAlbum,
        LinkArtist,
        LinkPodcast,
        LinkUser,
        LinkUnsupported
    };
    Q_ENUM(LinkTarget)

    ~App() override;

    static void install(Sources sources);
    // Null before install().
    [[nodiscard]] static App* instance();
    // QML singleton factory: the installed instance, owned by C++.
    static App* create(QQmlEngine* qml, QJSEngine* js);

    // Puts plain text on the system clipboard (copying a comment).
    Q_INVOKABLE static void copyText(const QString& text);
    // An orpheus:// link the web asked the client to open (a launch argument, open-links.md):
    // a song to play or a voice plays next at once; anything else goes to the shell as
    // linkRequested (LinkUnsupported when it can't be read). Logged without the link.
    void openLink(const QString& url);

    [[nodiscard]] AppSettings* settings() const { return m_settings; }
    [[nodiscard]] CacheStatus* cache() const { return m_cache; }
    [[nodiscard]] Diagnostics* diagnostics() const { return m_diagnostics; }
    [[nodiscard]] Formatter* format() const { return m_format; }
    [[nodiscard]] Library* library() const { return m_library; }
    [[nodiscard]] CurrentLyrics* lyrics() const { return m_lyrics; }
    [[nodiscard]] NetworkStatus* network() const { return m_network; }
    [[nodiscard]] UpdateCheck* updates() const { return m_updates; }
    [[nodiscard]] Together* together() const { return m_together; }
    [[nodiscard]] Share* share() const { return m_share; }
    [[nodiscard]] ShortcutMap* shortcuts() const { return m_shortcuts; }

    [[nodiscard]] Player* player() const { return m_player; }
    [[nodiscard]] Session* session() const { return m_session; }
    [[nodiscard]] LocalStats* stats() const { return m_stats; }
    [[nodiscard]] FakeControls* debug() const { return m_sources.fakeControls.get(); }

    [[nodiscard]] CatalogSource* catalog() const { return m_sources.catalog.get(); }
    [[nodiscard]] ImageSource* images() const { return m_sources.images.get(); }
    [[nodiscard]] LibrarySource* librarySource() const { return m_sources.library.get(); }
    [[nodiscard]] CommentSource* commentSource() const { return m_sources.comments.get(); }
    [[nodiscard]] StatsSource* statsSource() const { return m_sources.stats.get(); }

    // `image://cover/<key>` provider; the QML engine takes ownership.
    [[nodiscard]] QQuickAsyncImageProvider* makeCoverProvider();
    // `image://qr/<serial>` provider for the sign-in code; the engine takes ownership.
    [[nodiscard]] QQuickImageProvider* makeQrProvider();

  signals:
    // Shell.openLink: the page of `key` (digits), or a notice for LinkUnsupported (no key).
    void linkRequested(int target, const QString& key);
    // A link that couldn't play.
    void notice(const QString& message);

  private:
    explicit App(Sources sources);

    Sources m_sources;
    Request m_linkVoice; // the voice a link plays, while it is read
    AppSettings* m_settings;
    CacheStatus* m_cache;
    Diagnostics* m_diagnostics;
    Formatter* m_format;
    NetworkStatus* m_network;
    UpdateCheck* m_updates;
    Player* m_player;
    Session* m_session;
    Library* m_library;
    CurrentLyrics* m_lyrics;
    LocalStats* m_stats;
    Together* m_together;
    Share* m_share;
    ShortcutMap* m_shortcuts;
};

} // namespace cirrus::ui
