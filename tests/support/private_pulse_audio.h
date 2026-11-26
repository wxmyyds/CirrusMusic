#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QMediaDevices>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>

namespace cirrus::test
{
// Linux integration tests own this foreground server and one float stereo null
// sink. They never connect to, stop or reconfigure the user's audio server.
class PrivatePulseAudioServer
{
  public:
    ~PrivatePulseAudioServer() { stop(); }

    bool start()
    {
        if (!m_directory.isValid())
            return fail(m_directory.errorString());
        const QString runtimePath = m_directory.filePath(QStringLiteral("runtime"));
        const QString statePath = m_directory.filePath(QStringLiteral("state"));
        if (::mkdir(QFile::encodeName(runtimePath).constData(), 0700) != 0 ||
            ::mkdir(QFile::encodeName(statePath).constData(), 0700) != 0)
            return fail(QStringLiteral("Cannot create private PulseAudio directories"));
        m_socketPath = m_directory.filePath(QStringLiteral("native"));
        const QString sinkName = QDir(m_directory.path()).dirName();

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.remove(QStringLiteral("PULSE_SERVER"));
        environment.remove(QStringLiteral("PULSE_SINK"));
        environment.remove(QStringLiteral("PULSE_SOURCE"));
        environment.insert(QStringLiteral("PULSE_RUNTIME_PATH"), runtimePath);
        environment.insert(QStringLiteral("PULSE_STATE_PATH"), statePath);
        m_server.setProcessEnvironment(environment);
        m_server.setProcessChannelMode(QProcess::MergedChannels);
        QString escapedSocket = m_socketPath;
        escapedSocket.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        escapedSocket.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        m_server.start(
            QStringLiteral("pulseaudio"),
            {QStringLiteral("-n"), QStringLiteral("--daemonize=no"), QStringLiteral("--system=no"),
             QStringLiteral("--use-pid-file=no"), QStringLiteral("--exit-idle-time=-1"),
             QStringLiteral("--log-target=stderr"),
             QStringLiteral("--load=module-native-protocol-unix socket=\"%1\" auth-anonymous=1")
                 .arg(escapedSocket),
             QStringLiteral("--load=module-null-sink sink_name=%1 rate=48000 channels=2 "
                            "format=float32le sink_properties=device.description=%1")
                 .arg(sinkName)});
        if (!m_server.waitForStarted(1500))
            return fail(QStringLiteral("Cannot start private PulseAudio"));
        if (!QTest::qWaitFor([this] { return acceptsConnection(); }, 3000))
            return fail(QStringLiteral("Private PulseAudio socket did not become ready"));

        // Set these before the first QMediaDevices access. Keep the server
        // pinned through multimedia singleton teardown, even after stop().
        if (!qputenv("PULSE_SERVER", QByteArray("unix:") + m_socketPath.toUtf8()) ||
            !qputenv("QT_AUDIO_BACKEND", QByteArray("pulseaudio")))
            return fail(QStringLiteral("Cannot select private PulseAudio"));
        if (!QTest::qWaitFor(
                [&] { return QMediaDevices::defaultAudioOutput().description() == sinkName; },
                3000))
            return fail(QStringLiteral("Private null sink did not become the default output"));

        m_format.setSampleRate(48000);
        m_format.setChannelCount(2);
        m_format.setSampleFormat(QAudioFormat::Float);
        if (!QTest::qWaitFor(
                [&] { return QMediaDevices::defaultAudioOutput().isFormatSupported(m_format); },
                3000))
            return fail(QStringLiteral("Private null sink does not support float stereo 48 kHz"));
        m_device = QMediaDevices::defaultAudioOutput();
        return true;
    }

    bool stop()
    {
        if (m_server.state() == QProcess::NotRunning)
            return true;
        m_server.kill();
        return m_server.waitForFinished(1000);
    }

    QString errorString()
    {
        return QStringLiteral("%1\nPrivate pulseaudio: %2\n%3")
            .arg(m_failure, m_server.errorString(), QString::fromLocal8Bit(m_server.readAll()));
    }

    const QAudioDevice& device() const { return m_device; }
    const QAudioFormat& format() const { return m_format; }

  private:
    QTemporaryDir m_directory{
        QDir(QDir::currentPath()).filePath(QStringLiteral("cirrus-pulse_XXXXXX"))};
    QProcess m_server; // destroyed before its private directories
    QString m_socketPath;
    QString m_failure;
    QAudioDevice m_device;
    QAudioFormat m_format;

    bool fail(const QString& message)
    {
        m_failure = message;
        return false;
    }

    bool acceptsConnection()
    {
        if (m_server.state() != QProcess::Running)
            return false;
        QLocalSocket socket;
        socket.connectToServer(m_socketPath);
        const bool connected = socket.waitForConnected(25);
        socket.abort();
        return connected;
    }
};
} // namespace cirrus::test
