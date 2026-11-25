#include "now_playing_export.h"
#include "logging/log.h"

#include "app_settings.h"
#include "lyrics.h"
#include "player.h"

#include "application/backend/backend_facade.h"
#include "application/playback/listening_session.h"
#include "application/playback/playback_observation_publisher.h"
#include "infrastructure/runtime/backend_runtime.h"

#ifdef CIRRUS_NOW_PLAYING_EXPORT
#include "infrastructure/ipc/linux_observation_service.h"
#endif

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>

namespace cirrus::ui
{

namespace
{
// The directory under $XDG_RUNTIME_DIR that third parties look in (tools/now-playing).
constexpr auto kNamespace = "com.guang233.Cirrus";
constexpr qint64 kDrainMs = 2000;
constexpr int kDrainPollMs = 25;
} // namespace

#ifdef CIRRUS_NOW_PLAYING_EXPORT
struct NowPlayingExport::Service {
    // Declared first, so the service goes before the publisher it reads.
    std::unique_ptr<application::PlaybackObservationPublisher> publisher;
    std::unique_ptr<infrastructure::ipc::LinuxObservationService> service;
    // What was given for which load: details that come later replace it.
    std::optional<std::pair<domain::ObservationBinding, Song>> attached;
};
namespace
{
// Shutdown tickets of services still cleaning up on their own threads.
std::vector<std::shared_ptr<infrastructure::ipc::LinuxObservationShutdown>>& draining()
{
    static std::vector<std::shared_ptr<infrastructure::ipc::LinuxObservationShutdown>> tickets;
    return tickets;
}
bool drained()
{
    auto& tickets = draining();
    std::erase_if(tickets, [](const auto& t) { return t->complete.load(); });
    return tickets.empty();
}
} // namespace
#else
struct NowPlayingExport::Service {};
namespace
{
bool drained() { return true; }
} // namespace
#endif

NowPlayingExport::NowPlayingExport(runtime::BackendRuntime& runtime, QObject* parent)
    : QObject(parent), m_runtime(runtime)
{
}

NowPlayingExport::~NowPlayingExport() { stop(); }

bool NowPlayingExport::available()
{
#ifdef CIRRUS_NOW_PLAYING_EXPORT
    return true;
#else
    return false;
#endif
}

void NowPlayingExport::follow(AppSettings* settings, Player* player, CurrentLyrics* lyrics)
{
    connect(player, &Player::currentChanged, this, [this, player] {
        const auto& current = player->current();
        if (current.key.isEmpty()) {
            m_song.reset();
            return;
        }
        Song song{current.key, current.title, current.albumTitle, {}};
        for (const auto& artist : current.artists)
            song.artists.append(artist.toMap().value(QStringLiteral("name")).toString());
        m_song = std::move(song);
        attach();
    });
    // The backend's lyric cursors take the same offset the lyrics view shows (positive: later).
    const auto offset = [this, lyrics] {
        m_offsetMs = std::clamp<qint64>(lyrics->offsetMs(), -120000, 120000);
        if (m_service)
            m_runtime.listening().setLyricOffset(m_offsetMs);
    };
    connect(lyrics, &CurrentLyrics::offsetChanged, this, offset);
    offset();
    connect(settings, &AppSettings::shareNowPlayingChanged, this,
            [this, settings] { setEnabled(settings->shareNowPlaying()); });
    setEnabled(settings->shareNowPlaying());
}

void NowPlayingExport::setEnabled(bool enabled)
{
    if (!enabled || m_closed) {
        stop();
        return;
    }
    if (m_service)
        return;
#ifdef CIRRUS_NOW_PLAYING_EXPORT
    using infrastructure::ipc::LinuxEndpointError;
    using infrastructure::ipc::LinuxObservationService;
    using infrastructure::ipc::LinuxObservationServiceConfig;
    using infrastructure::ipc::LinuxObservationServiceState;
    auto service = std::make_unique<Service>();
    service->publisher = std::make_unique<application::PlaybackObservationPublisher>(
        m_runtime.listening(), m_runtime.facade());
    service->service = std::make_unique<LinuxObservationService>(*service->publisher);
    connect(service->publisher.get(), &application::PlaybackObservationPublisher::changed, this,
            [this] { attach(); });
    connect(service->service.get(), &LinuxObservationService::stateChanged, this,
            [this](LinuxObservationServiceState state, LinuxEndpointError error) {
                if (state == LinuxObservationServiceState::Running) {
                    log::info(lcPlatform, "now_playing.started");
                } else if (state == LinuxObservationServiceState::Failed) {
                    log::warn(lcPlatform, "now_playing.start_failed", {"err", error});
                    // Not from inside the service's own signal.
                    QTimer::singleShot(0, this, [this] {
                        stop();
                        emit failed();
                    });
                }
            });
    m_service = std::move(service);
    m_runtime.listening().setLyricOffset(m_offsetMs);
    if (!m_service->service->start(LinuxObservationServiceConfig::fromXdg(true, kNamespace))) {
        log::warn(lcPlatform, "now_playing.start_failed",
                  {"err", LinuxEndpointError::InvalidConfig});
        QTimer::singleShot(0, this, [this] {
            stop();
            emit failed();
        });
        return;
    }
    attach();
#endif
}

void NowPlayingExport::attach()
{
#ifdef CIRRUS_NOW_PLAYING_EXPORT
    // The song's details may come after it starts playing: nothing until they have.
    if (!m_service || !m_song || m_song->title.isEmpty())
        return;
    const auto now = m_service->publisher->snapshot();
    if (!now.song || now.song->value != m_song->key ||
        m_service->attached == std::pair{now.binding, *m_song})
        return;
    domain::Song song;
    song.id = *now.song;
    song.title = m_song->title;
    for (const auto& name : m_song->artists)
        song.artists.append({std::nullopt, name});
    if (!m_song->album.isEmpty())
        song.album = domain::AlbumReference{std::nullopt, m_song->album, {}};
    // Once per load and details: a rejected one (too long, say) stays unknown.
    m_service->attached = std::pair{now.binding, *m_song};
    m_service->publisher->attachMetadata(now.binding, song);
#endif
}

void NowPlayingExport::stop()
{
    if (!m_service)
        return;
#ifdef CIRRUS_NOW_PLAYING_EXPORT
    m_service->service->close();
    draining().push_back(m_service->service->shutdownTicket());
    log::info(lcPlatform, "now_playing.stopped");
#endif
    m_service.reset();
}

void NowPlayingExport::close(std::function<void()> done)
{
    m_closed = true;
    stop();
    if (drained()) {
        done();
        return;
    }
    auto* poll = new QTimer(this);
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    connect(poll, &QTimer::timeout, this, [poll, elapsed, done = std::move(done)] {
        if (!drained() && elapsed->elapsed() < kDrainMs)
            return;
        if (!drained())
            log::warn(lcPlatform, "now_playing.quit_before_cleanup");
        poll->stop();
        poll->deleteLater();
        done();
    });
    poll->start(kDrainPollMs);
}

} // namespace cirrus::ui
