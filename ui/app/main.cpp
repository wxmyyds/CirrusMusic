#include "app.h"
#include "app_icon.h"
#include "app_settings.h"
#include "backend_host.h"
#include "backend_sources.h"
#include "cirrus_commit.h"
#include "desktop.h"
#include "diagnostics.h"
#include "gpu_preference.h"
#include "logging/log.h"
#include "logging/logger.h"
#include "memory.h"
#ifdef CIRRUS_UI_MPRIS
#include "mpris.h"
#endif
#ifdef CIRRUS_UI_NOW_PLAYING
#include "now_playing.h"
#endif
#ifdef CIRRUS_UI_SMTC
#include "smtc.h"
#endif
#include "single_instance.h"
#include "title_bar_mode.h"
#include "update_check.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#ifdef CIRRUS_UI_TRAY
// QSystemTrayIcon needs QApplication (docs/10-linux-integration.md §4).
#include <QApplication>
using Application = QApplication;
#else
#include <QGuiApplication>
using Application = QGuiApplication;
#endif
#include <QtQml/QQmlExtensionPlugin>

#include <memory>

Q_IMPORT_QML_PLUGIN(Cirrus_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_WindowPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_PresentationPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_ComponentsPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_PlayerPlugin)
Q_IMPORT_QML_PLUGIN(Cirrus_SamplesPlugin)
Q_IMPORT_QML_PLUGIN(M3ShapesPlugin)

namespace
{

// The settings lived in ~/.config/Cirrus/CirrusMusic.conf before the app identity was
// decided (UQ-06); they move to the app's own file once.
void moveOldSettings()
{
    const QString now = QSettings().fileName();
    const QString before =
        QSettings(QStringLiteral("Cirrus"), QStringLiteral("CirrusMusic")).fileName();
    if (!QFile::exists(now) && QFile::exists(before))
        QFile::rename(before, now);
}

// The key that makes account ids in the log unreadable but comparable (docs/tasks/logging.md
// §5); kept in the settings, which a diagnostic package never contains.
QByteArray accountKey()
{
    QSettings settings;
    const QString name = QStringLiteral("diagnostics/accountKey");
    QByteArray key = QByteArray::fromBase64(settings.value(name).toByteArray());
    if (key.size() < 32) {
        key.resize(32);
        QRandomGenerator::system()->generate(key.begin(), key.end());
        settings.setValue(name, key.toBase64());
    }
    return key;
}

QString graphicsName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Software:
        return QStringLiteral("software");
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("opengl");
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("vulkan");
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("d3d11");
    case QSGRendererInterface::Direct3D12:
        return QStringLiteral("d3d12");
    case QSGRendererInterface::Metal:
        return QStringLiteral("metal");
    default:
        return QString::number(int(api));
    }
}

// The log's session file (docs/tasks/logging.md §6), with the roots log::path() hides.
void startLog()
{
    namespace log = cirrus::log;
    const auto location = [](QStandardPaths::StandardLocation l) {
        return QStandardPaths::writableLocation(l) + QStringLiteral("/Cirrus");
    };
    const QString logs = log::standardDirectory(QStringLiteral("Cirrus"));
    log::setPathRoots(
        {{QStringLiteral("<data>"), location(QStandardPaths::GenericDataLocation)},
         {QStringLiteral("<cache>"), location(QStandardPaths::GenericCacheLocation)},
         {QStringLiteral("<config>"), location(QStandardPaths::GenericConfigLocation)},
         {QStringLiteral("<logs>"), logs},
         {QStringLiteral("<tmp>"), QDir::tempPath()}});
    log::setAccountKey(accountKey());
    log::FileOptions options;
    options.directory = logs;
    options.header = QStringLiteral("commit=") + QStringLiteral(CIRRUS_COMMIT);
    if (!log::start(options))
        log::warn(lcLog, "log.file_unavailable", {"dir", log::path(logs)});
}

} // namespace

int main(int argc, char* argv[])
{
    // Before anything allocates on another thread.
    cirrus::ui::memory::configure();
    // Every record from here on goes through the app's log (docs/tasks/logging.md); it
    // is the last thing to go.
    cirrus::log::Session logging;
    // The app's identity (UQ-06, docs/ui/integration.md §6 D1). Set first: the GPU
    // choice reads the settings before the application object exists.
    QCoreApplication::setOrganizationName(QStringLiteral("Cirrus"));
    QCoreApplication::setApplicationName(QStringLiteral("Cirrus"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CIRRUS_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("com.guang233.Cirrus"));
    moveOldSettings();
    cirrus::ui::gpu::apply(cirrus::ui::AppSettings().preferDiscreteGpu());
    // Qt Multimedia only plays audio here. Left alone, its FFmpeg backend opens every
    // hardware video device when it starts (CUDA, VDPAU, Vulkan): a CUDA context on the
    // discrete GPU, with its thread and memory, for nothing.
    for (const char* name :
         {"QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "QT_FFMPEG_ENCODING_HW_DEVICE_TYPES"}) {
        if (!qEnvironmentVariableIsSet(name))
            qputenv(name, QByteArray(""));
    }

    Application app(argc, argv);
    QGuiApplication::setWindowIcon(cirrus::ui::appIcon());

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(cirrus::ui::titlebar::option());
    const QCommandLineOption logLevel(QStringLiteral("log-level"),
                                      QStringLiteral("info (default) or debug, for this run."),
                                      QStringLiteral("level"));
    parser.addOption(logLevel);
    parser.process(app);
    if (parser.value(logLevel) == u"debug")
        cirrus::log::setVerbose(true);

    // A second launch shows the running app's window instead.
    cirrus::ui::SingleInstance instance;
    if (!instance.claim())
        return 0;
    startLog();
    cirrus::log::info(lcApp, "app.platform",
                      {{"qpa", QGuiApplication::platformName()},
                       {"graphics", graphicsName(QQuickWindow::graphicsApi())},
                       {"verbose", cirrus::log::verbose()}});

    // The backend (UI phase 5, docs/ui/integration.md): closed before the app quits.
    QString unwritable;
    const auto paths = cirrus::ui::BackendHost::preparePaths(&unwritable);
    if (!paths) {
        cirrus::log::critical(lcApp, "app.paths_unavailable",
                              {"dir", cirrus::log::path(unwritable)});
        return 1;
    }
    cirrus::ui::BackendHost backend(*paths);

    // The backend's sources, fakes for what they don't cover yet.
    cirrus::ui::App::install(cirrus::ui::makeBackendSources(backend));
    auto* presentation = cirrus::ui::App::instance();
    presentation->updates()->setPackage(QStringLiteral(CIRRUS_PACKAGE));
    cirrus::ui::Desktop desktop(&instance);
    desktop.setShutdown(
        [&backend](std::function<void()> done) { backend.shutdown(std::move(done)); });
    backend.start();
#ifdef CIRRUS_UI_MPRIS
    // Media keys and the desktop's media controls (docs/ui/pages/shell.md §7).
    cirrus::ui::Mpris mpris(presentation->player(), &desktop, presentation->lyrics(),
                           presentation->settings());
#endif
#ifdef CIRRUS_UI_NOW_PLAYING
    cirrus::ui::NowPlaying nowPlaying(presentation->player());
#endif

    // --titlebar, then CIRRUS_TITLEBAR, then the user's setting, then the desktop default.
    namespace titlebar = cirrus::ui::titlebar;
    const int savedTitleBar = presentation->settings()->titleBar();
    const titlebar::Mode titleBar = titlebar::requested(parser).value_or(
        savedTitleBar >= 0 ? titlebar::Mode(savedTitleBar) : titlebar::platformDefault());

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    presentation->diagnostics()->watch(&engine);
    engine.addImageProvider(QStringLiteral("cover"), presentation->makeCoverProvider());
    engine.addImageProvider(QStringLiteral("qr"), presentation->makeQrProvider());
    engine.setInitialProperties({{QStringLiteral("titleBar"), int(titleBar)}});
    engine.loadFromModule("CirrusMusic", "Main");
#ifdef CIRRUS_UI_SMTC
    // Bound to the main window; gone before it is (declared after the engine).
    std::unique_ptr<cirrus::ui::Smtc> smtc;
#endif

    // Hidden in the tray, the window gives back its GPU device and scene graph, so a
    // discrete GPU can power down during hours of background playback.
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0))) {
        presentation->diagnostics()->watch(window);
        window->setPersistentGraphics(false);
        window->setPersistentSceneGraph(false);
        // ... and the heap gives back what that freed, once it is all gone.
        QObject::connect(window, &QWindow::visibleChanged, window, [window](bool visible) {
            if (visible)
                return;
            QTimer::singleShot(2000, window, [window]() {
                if (window->isVisible())
                    return;
                const int before = cirrus::ui::memory::residentMb();
                cirrus::ui::memory::trim();
                cirrus::log::info(
                    lcApp, "app.memory_trimmed",
                    {{"before_mb", before}, {"after_mb", cirrus::ui::memory::residentMb()}});
            });
        });
#ifdef CIRRUS_UI_SMTC
        smtc = std::make_unique<cirrus::ui::Smtc>(presentation->player(), window);
#endif
    }

    const int code = QGuiApplication::exec();
    cirrus::log::info(lcApp, "app.quit", {"code", code});
    return code;
}
