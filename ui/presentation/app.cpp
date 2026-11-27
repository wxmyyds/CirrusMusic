#include "app.h"
#include "app_link.h"
#include "logging/log.h"
#include "notice.h"

#include "app_settings.h"
#include "cache_status.h"
#include "cover_provider.h"
#include "diagnostics.h"
#include "fake_controls.h"
#include "formatter.h"
#include "library.h"
#include "lyrics.h"
#include "network_status.h"
#include "player.h"
#include "qr_provider.h"
#include "session.h"
#include "sharing.h"
#include "shortcut_map.h"
#include "stats_model.h"
#include "together.h"
#include "update_check.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJSEngine>

namespace cirrus::ui
{

namespace
{
App* g_instance = nullptr;
} // namespace

Sources::Sources() = default;
Sources::Sources(Sources&&) noexcept = default;
Sources& Sources::operator=(Sources&&) noexcept = default;
Sources::~Sources() = default;

App::App(Sources sources)
    : m_sources(std::move(sources)), m_settings(new AppSettings(this)),
      m_cache(new CacheStatus(m_sources.cache.get(), m_settings, this)),
      m_diagnostics(new Diagnostics(m_settings, this)), m_format(new Formatter(this)),
      m_network(new NetworkStatus(this)), m_updates(new UpdateCheck(m_settings, this)),
      m_player(new Player(m_sources.playback.get(), m_settings, this)),
      m_session(new Session(m_sources.session.get(), this)),
      m_library(new Library(m_sources.library.get(), m_session, this)),
      m_lyrics(new CurrentLyrics(m_sources.lyrics.get(), m_player, this)),
      m_stats(new LocalStats(m_sources.stats.get(), this)),
      m_together(new Together(m_sources.together.get(), this)),
      m_share(new Share(m_settings, this)), m_shortcuts(new ShortcutMap(this))
{
    if (m_sources.fakeControls)
        m_sources.fakeControls->attach(m_network);
    // 心动模式 is the liked songs' (Player::heartbeatAvailable).
    m_player->setLikedPlaylist(m_library->likedPlaylistKey());
    connect(m_library, &Library::likedPlaylistChanged, m_player,
            [this] { m_player->setLikedPlaylist(m_library->likedPlaylistKey()); });
}

App::~App()
{
    if (g_instance == this)
        g_instance = nullptr;
}

void App::install(Sources sources)
{
    Q_ASSERT_X(!g_instance, "App::install", "installed twice");
    g_instance = new App(std::move(sources));
    // Destroyed with the application, after the QML engine in main().
    g_instance->setParent(QCoreApplication::instance());
}

App* App::instance() { return g_instance; }

App* App::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT_X(g_instance, "App::create", "call App::install() before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

void App::openLink(const QString& url)
{
    const auto request = applink::parse(url);
    log::info(lcUi, "link.open",
              {{"read", request.has_value()},
               log::fieldIf(request.has_value(), "target", request ? int(request->target) : -1),
               log::fieldIf(request.has_value(), "play", request && request->play)});
    if (!request) {
        emit linkRequested(LinkUnsupported, {});
        return;
    }
    if (request->target == applink::Target::Song && request->play) {
        m_player->playNow(request->id, false);
    } else if (request->target == applink::Target::Voice) {
        // Read first: a voice plays its track (podcast-playback.md).
        const QString key = request->id;
        m_linkVoice = catalog()->voice(key, [this, key](const Outcome<SongInfo>& outcome) {
            m_linkVoice.release();
            if (outcome.value && !outcome.value->trackKey.isEmpty())
                m_player->playNow(key, true);
            else
                emit notice(noticed("link.voice_unplayable", tr("这期声音播放不了")));
        });
    } else {
        emit linkRequested(int(request->target), request->id);
    }
}

void App::copyText(const QString& text)
{
    if (QClipboard* clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

QQuickAsyncImageProvider* App::makeCoverProvider() { return new CoverProvider(images()); }

QQuickImageProvider* App::makeQrProvider() { return new QrProvider(m_session); }

} // namespace cirrus::ui
