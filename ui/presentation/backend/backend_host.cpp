#include "backend_host.h"
#include "logging/log.h"

#include "application/backend/backend_facade.h"
#include "application/playback/listening_reporter.h"
#include "application/playback/listening_session.h"
#include "application/playback/together_session.h"
#include "infrastructure/runtime/backend_runtime.h"

#include <QDir>
#include <QFile>
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

// Makes `path` and leaves it readable by the owner only.
bool makePrivateDirectory(const QString& path)
{
    if (!QDir().mkpath(path))
        return false;
    return QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

} // namespace

std::optional<BackendHost::Paths> BackendHost::preparePaths(QString* error)
{
    const QString data =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u'/' + kDirectory;
    const QString cacheBase =
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + u'/' + kDirectory;
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

BackendHost::~BackendHost() = default; // together and the reporter go first (declared last)

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
    if (auto done = std::exchange(m_done, {}))
        done();
}

} // namespace cirrus::ui
