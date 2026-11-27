#include "single_instance.h"
#include "app_link.h"
#include "logging/log.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace cirrus::ui
{

namespace
{

constexpr qint64 kMaxMessage = 4096;
constexpr int kWaitMs = 1000;

const QByteArray kActivate = QByteArrayLiteral("activate");
const QByteArray kOpen = QByteArrayLiteral("open");

// The app's private runtime directory (0700).
QString runtimeDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    const QString dir = base + QLatin1Char('/') + QCoreApplication::applicationName();
    QDir().mkpath(dir);
    QFile::setPermissions(dir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return dir;
}

QString serverName(const QString& dir)
{
#ifdef Q_OS_WIN
    // A pipe name, not a path; unique per user and app.
    return QCoreApplication::applicationName() + QLatin1Char('-') +
           QString::fromLatin1(
               QCryptographicHash::hash(dir.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
#else
    return dir + QStringLiteral("/instance.socket");
#endif
}

bool validToken(const QByteArray& token)
{
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9._:@/+=-]{1,256}$"));
    return allowed.match(QString::fromLatin1(token)).hasMatch();
}

} // namespace

SingleInstance::SingleInstance(QObject* parent) : QObject(parent) {}
SingleInstance::SingleInstance(QString runtimeDirectory, QObject* parent)
    : QObject(parent), m_runtimeDirectory(std::move(runtimeDirectory))
{
}

SingleInstance::~SingleInstance() = default;

SingleInstance::ClaimResult SingleInstance::claim(const QString& link)
{
    const QString dir = m_runtimeDirectory.isEmpty() ? runtimeDir() : m_runtimeDirectory;
    if (!QDir().mkpath(dir)) {
        log::warn(lcApp, "app.single_instance_runtime_failed", {"dir", log::path(dir)});
        return ClaimResult::Failed;
    }
    QFile::setPermissions(dir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString name = serverName(dir);
    m_lock = std::make_unique<QLockFile>(dir + QStringLiteral("/instance.lock"));
    // Stale only when its process is gone, never by age.
    m_lock->setStaleLockTime(0);
    if (m_lock->tryLock(100)) {
        if (listen(name))
            return ClaimResult::Started;
        m_lock.reset();
        return ClaimResult::Failed;
    }

    QLocalSocket socket;
    socket.connectToServer(name);
    if (socket.waitForConnected(kWaitMs)) {
        QByteArray message = kActivate;
        const QByteArray token = qgetenv("XDG_ACTIVATION_TOKEN");
        if (applink::plausible(link))
            message = kOpen + ' ' + (validToken(token) ? token : QByteArrayLiteral("-")) + ' ' +
                      link.toLatin1();
        else if (validToken(token))
            message += ' ' + token;
        message += '\n';
        if (socket.write(message) != message.size() || !socket.waitForBytesWritten(kWaitMs)) {
            log::warn(lcApp, "app.single_instance_send_failed", {"err", socket.error()});
            return ClaimResult::Failed;
        }
        socket.disconnectFromServer();
        return ClaimResult::Activated;
    }
    log::warn(lcApp, "app.single_instance_unavailable", {"lock", log::path(m_lock->fileName())});
    m_lock.reset();
    return ClaimResult::Failed;
}

bool SingleInstance::listen(const QString& name)
{
    // A socket file left by a crashed run.
    QLocalServer::removeServer(name);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket* socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { read(socket); });
            // A client that never finishes its line is dropped.
            QTimer::singleShot(kWaitMs, socket, &QLocalSocket::abort);
        }
    });
    if (!m_server->listen(name)) {
        log::warn(lcApp, "app.single_instance_listen_failed", {"err", m_server->serverError()});
        delete m_server;
        m_server = nullptr;
        return false;
    }
    return true;
}

void SingleInstance::read(QLocalSocket* socket)
{
    if (!socket->canReadLine()) {
        if (socket->bytesAvailable() > kMaxMessage)
            socket->abort();
        return;
    }
    const QList<QByteArray> parts = socket->readLine(kMaxMessage).trimmed().split(' ');
    socket->abort();
    const bool open = parts.size() == 3 && parts.first() == kOpen;
    if (!open && (parts.size() > 2 || parts.first() != kActivate))
        return;
    const QByteArray token = parts.value(1);
    emit activateRequested(validToken(token) ? QString::fromLatin1(token) : QString());
    if (open && applink::plausible(QString::fromLatin1(parts.at(2))))
        emit linkRequested(QString::fromLatin1(parts.at(2)));
}

} // namespace cirrus::ui
