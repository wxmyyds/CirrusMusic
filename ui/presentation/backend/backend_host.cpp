#include "backend_host.h"
#include "logging/log.h"

#include "application/backend/backend_facade.h"
#include "application/playback/listening_reporter.h"
#include "application/playback/listening_session.h"
#include "application/playback/together_session.h"
#include "infrastructure/runtime/backend_runtime.h"
#include "infrastructure/storage/private_files.h"
#include "now_playing_export.h"

#include <QDir>
#include <QFileDevice>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>

namespace cirrus::ui
{
namespace
{

// The app's directory name and keyring service (UQ-06, integration.md §6 D1).
const QString kDirectory = QStringLiteral("Cirrus");
const QString kKeychainService = QStringLiteral("com.guang233.Cirrus");
// How long to wait after the backend reported a delayed close before quitting anyway.
constexpr int kGraceMs = 5000;

// Set permissions at creation: native ACLs on Windows, owner-only mode on Unix.
// Existing directories are checked, never chmodded through a symlink.
bool makePrivateDirectory(const QString& path)
{
    return QDir().mkpath(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                   QFileDevice::ExeOwner) &&
           storage::isPrivateDirectory(path);
}

} // namespace

std::optional<BackendHost::Paths> BackendHost::preparePaths(QString* error)
{
    return preparePaths(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
                        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation),
                        error);
}

std::optional<BackendHost::Paths>
BackendHost::preparePaths(const QString& dataLocation, const QString& cacheLocation, QString* error)
{
    if (error)
        error->clear();
    for (const auto& location : {dataLocation, cacheLocation}) {
        if (location.isEmpty() || !QDir::isAbsolutePath(location) ||
            QDir::cleanPath(location) != location) {
            if (error)
                *error = location.isEmpty() ? QStringLiteral("<empty storage location>") : location;
            return std::nullopt;
        }
    }
    const QString data = dataLocation + u'/' + kDirectory;
    const QString cacheBase = cacheLocation + u'/' + kDirectory;
    const QString cache = cacheBase + QStringLiteral("/resources");
    const QString audio = cacheBase + QStringLiteral("/audio");
    for (const QString& path : {data, cache, audio}) {
        if (!makePrivateDirectory(path)) {
            if (error)
                *error = path;
            return std::nullopt;
        }
    }
    return Paths{data + QStringLiteral("/state.sqlite"), cache, kKeychainService, audio};
}

BackendHost::BackendHost(const Paths& paths, QObject* parent)
    : QObject(parent), m_runtime(std::make_unique<runtime::BackendRuntime>(runtime::BackendConfig{
                           .databasePath = paths.database,
                           .keychainService = paths.keychainService,
                           .persistListeningState = true,
                           .resourceCacheRoot = paths.cache,
                           .audioCacheRoot = paths.audioCache,
                       }))
{
    auto& facade = m_runtime->facade();
    m_reporter = std::make_unique<application::ListeningReporter>(facade, m_runtime->listening());
    m_together = std::make_unique<application::TogetherSession>(facade, m_runtime->listening());
    m_nowPlaying = std::make_unique<NowPlayingExport>(*m_runtime);
    connect(&facade, &application::BackendFacade::started, this,
            [](application::StartupError error) {
                if (error == application::StartupError::None)
                    log::info(lcUiBackend, "backend.ready");
                else if (error == application::StartupError::GuestBootstrap)
                    log::warn(lcUiBackend, "backend.ready_without_guest");
                else
                    log::critical(lcUiBackend, "backend.start_failed", {"err", error});
            });
    connect(&facade, &application::BackendFacade::persistenceWarning, this,
            [](application::PersistenceArea area) {
                log::warn(lcUiBackend, "backend.persistence_warning", {"area", area});
            });
    connect(&facade, &application::BackendFacade::stopped, this, [this](bool persistenceFailed) {
        if (persistenceFailed)
            log::warn(lcUiBackend, "backend.stopped_unsaved");
        finish();
    });
    connect(&facade, &application::BackendFacade::closeDelayed, this, [this] {
        log::warn(lcUiBackend, "backend.close_delayed");
        QTimer::singleShot(kGraceMs, this, [this] {
            if (m_done) {
                log::warn(lcUiBackend, "backend.quit_before_stopped");
                finish();
            }
        });
    });
}

// The export, together and the reporter go first (declared last).
BackendHost::~BackendHost() = default;

void BackendHost::start() { m_runtime->facade().start(); }

void BackendHost::shutdown(std::function<void()> done)
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    m_done = std::move(done);
    const auto state = m_runtime->facade().state();
    if (state == application::BackendState::Stopped) {
        finish();
        return;
    }
    // Listen together keeps its room for the next start (BackendTogether saves it); the song
    // playing now is reported first (at most 2 s), then the backend closes.
    m_together->close();
    m_reporter->flush([this] {
        if (m_runtime->facade().state() != application::BackendState::Stopped)
            m_runtime->facade().close();
        else
            finish();
    });
}

void BackendHost::finish()
{
    // The now-playing socket and files are removed on their own thread before the app quits.
    if (auto done = std::exchange(m_done, {}))
        m_nowPlaying->close(std::move(done));
}

} // namespace cirrus::ui
