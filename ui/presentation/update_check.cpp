#include "update_check.h"

#include "app_settings.h"
#include "logging/categories.h"
#include "logging/log.h"
#include "notice.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace cirrus::ui
{
namespace
{
constexpr qint64 kFirstDelayMs = 30 * 1000;         // after the start: not with its requests
constexpr qint64 kIntervalMs = 24 * 60 * 60 * 1000; // between checks of its own
constexpr int kTimeoutMs = 15 * 1000;
// The installer's download is aborted after this long without a byte.
constexpr int kDownloadStallMs = 30 * 1000;

const char* outcomeName(UpdateCheck::Status status)
{
    switch (status) {
    case UpdateCheck::Available:
        return "available";
    case UpdateCheck::UpToDate:
        return "current";
    default:
        return "failed";
    }
}
} // namespace

UpdateCheck::UpdateCheck(AppSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    m_sinceStart.start();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { start(false); });
    connect(m_settings, &AppSettings::updatesAutoChanged, this, &UpdateCheck::schedule);
}

qint64 UpdateCheck::publishedAt() const
{
    return m_release.published.isValid() ? m_release.published.toMSecsSinceEpoch() : 0;
}

qint64 UpdateCheck::lastCheck() const
{
    return std::max<qint64>(0, m_settings->updatesLastCheck());
}

bool UpdateCheck::canCheckAutomatically() const { return m_install != Development; }

void UpdateCheck::setPackage(const QString& package)
{
    if (package == QLatin1String("linux")) {
        // The deb and the AppImage share one build; the AppImage runtime sets APPIMAGE.
        m_install = qEnvironmentVariableIsSet("APPIMAGE") ? AppImage : Deb;
    } else if (package == QLatin1String("windows")) {
        // The installer and the zip share one build; Inno Setup leaves its uninstaller.
        const QDir dir(QCoreApplication::applicationDirPath());
        m_install = QFileInfo::exists(dir.filePath(QStringLiteral("unins000.exe"))) ? WindowsSetup
                                                                                    : WindowsZip;
        // An installer left from an earlier update (or one never run) is not kept.
        if (m_install == WindowsSetup)
            removeDownloads();
    } else if (package == QLatin1String("macos")) {
        m_install = MacDmg;
    } else if (package == QLatin1String("aur")) {
        m_install = Aur;
    } else {
        m_install = Development;
    }
    schedule();
}

void UpdateCheck::schedule()
{
    m_timer.stop();
    if (!canCheckAutomatically() || !m_settings->updatesAuto())
        return;
    // Not before 30 s after the start, nor within 24 h of the last check.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = m_settings->updatesLastCheck();
    qint64 delay = kFirstDelayMs - m_sinceStart.elapsed();
    if (last > 0 && last <= now)
        delay = std::max(delay, last + kIntervalMs - now);
    m_timer.start(int(std::clamp<qint64>(delay, 0, kIntervalMs)));
}

void UpdateCheck::check() { start(true); }

void UpdateCheck::start(bool manual)
{
    if (m_reply)
        return;
    log::info(lcUi, "update.check",
              {"trigger", manual ? QStringLiteral("manual") : QStringLiteral("auto")});
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(
        QUrl(QStringLiteral("https://api.github.com/repos/Guang233/CirrusMusic/releases/latest")));
    // A plain client name; nothing that identifies the user or this installation.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Cirrus/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(kTimeoutMs);
    m_reply = m_network->get(request);
    setState(Checking, NoFailure);
    QNetworkReply* reply = m_reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, manual] { finish(reply, manual); });
}

void UpdateCheck::finish(QNetworkReply* reply, bool manual)
{
    reply->deleteLater();
    m_reply.clear();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Status status = Failed;
    Failure failure = NoFailure;
    if (httpStatus == 200) {
        if (const auto release = updates::parseRelease(reply->readAll())) {
            // A download of another version is no longer wanted.
            if (release->version != m_release.version && m_download != NoDownload) {
                cancelDownload();
                removeDownloads();
                m_installer.clear();
                setDownload(NoDownload);
            }
            m_release = *release;
            status = updates::isNewer(release->version, QCoreApplication::applicationVersion())
                         ? Available
                         : UpToDate;
        } else {
            failure = Invalid;
        }
    } else if (httpStatus == 404) { // no published release yet
        m_release = {};
        status = UpToDate;
    } else if (httpStatus == 403 || httpStatus == 429) { // GitHub's rate limit
        failure = Limited;
    } else {
        failure = Network;
    }

    log::info(lcUi, "update.result",
              {{"outcome", QString::fromLatin1(outcomeName(status))},
               {"status", httpStatus},
               log::fieldIf(status != Failed && !m_release.version.isEmpty(), "latest",
                            m_release.version),
               log::fieldIf(reply->error() != QNetworkReply::NoError, "err",
                            log::message(reply->errorString()))});

    m_settings->setUpdatesLastCheck(QDateTime::currentMSecsSinceEpoch());
    // A failure is told only for a check by hand; one on its own leaves what was known.
    if (status == Failed && !manual) {
        status = updates::isNewer(m_release.version, QCoreApplication::applicationVersion())
                     ? Available
                     : Idle;
        failure = NoFailure;
    }
    setState(status, failure);
    // Found on its own: told once for each version (the about settings keep showing it).
    if (!manual && status == Available && m_settings->updatesNotified() != m_release.version) {
        m_settings->setUpdatesNotified(m_release.version);
        emit notice(noticed("update.available", tr("Cirrus %1 可以更新").arg(m_release.version)));
    }
    schedule();
}

void UpdateCheck::setState(Status status, Failure failure)
{
    m_status = status;
    m_failure = failure;
    emit changed();
}

bool UpdateCheck::openRelease() const
{
    if (!updates::isReleasePage(m_release.page))
        return false;
    log::info(lcUiNav, "update.open_release");
    return QDesktopServices::openUrl(m_release.page);
}

bool UpdateCheck::canInstall() const
{
    return m_install == WindowsSetup && m_release.installer &&
           updates::isNewer(m_release.version, QCoreApplication::applicationVersion());
}

qreal UpdateCheck::downloadProgress() const
{
    if (!m_release.installer || m_release.installer->size <= 0)
        return 0;
    return std::clamp(qreal(m_received) / qreal(m_release.installer->size), qreal(0), qreal(1));
}

QString UpdateCheck::downloadFolder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
           QStringLiteral("/Cirrus/updates");
}

void UpdateCheck::removeDownloads() const
{
    // Only the app's own folder for them, under its cache.
    QDir folder(downloadFolder());
    if (folder.exists())
        folder.removeRecursively();
}

void UpdateCheck::setDownload(Download download)
{
    m_download = download;
    emit downloadChanged();
}

void UpdateCheck::downloadInstaller()
{
    if (!canInstall() || m_downloadReply)
        return;
    const updates::Asset asset = *m_release.installer;
    removeDownloads();
    m_installer.clear();
    const QString folder = downloadFolder();
    delete m_downloadFile;
    m_downloadFile = new QFile(folder + u'/' + asset.name + QStringLiteral(".part"), this);
    if (!QDir().mkpath(folder) ||
        !QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner) ||
        !m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        log::warn(lcUi, "update.download_failed", {"reason", QStringLiteral("file")});
        setDownload(DownloadFailed);
        return;
    }
    m_downloadHash.reset();
    m_received = 0;
    m_downloadClock.start();
    log::info(lcUi, "update.download",
              {{"latest", m_release.version}, {"size_kb", asset.size / 1024}});

    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    // GitHub redirects to its file host; Qt follows https redirects only (its default policy).
    QNetworkRequest request(asset.url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Cirrus/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(kDownloadStallMs);
    m_downloadReply = m_network->get(request);
    QNetworkReply* reply = m_downloadReply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, size = asset.size] {
        const QByteArray data = reply->readAll();
        m_received += data.size();
        // Never more than GitHub said the file has.
        if (m_received > size || m_downloadFile->write(data) != data.size()) {
            reply->abort();
            return;
        }
        m_downloadHash.addData(data);
        emit downloadChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { downloadFinished(reply); });
    setDownload(Downloading);
}

void UpdateCheck::downloadFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    m_downloadReply.clear();
    m_downloadFile->close();
    if (!m_release.installer) {
        QFile::remove(m_downloadFile->fileName());
        return;
    }
    const updates::Asset asset = *m_release.installer;
    const QString part = m_downloadFile->fileName();
    const bool canceled = reply->error() == QNetworkReply::OperationCanceledError &&
                          m_received <= asset.size && m_download == NoDownload;
    QString reason;
    if (reply->error() != QNetworkReply::NoError)
        reason = m_received > asset.size ? QStringLiteral("size") : QStringLiteral("network");
    else if (m_received != asset.size)
        reason = QStringLiteral("size");
    else if (m_downloadHash.result() != asset.sha256)
        reason = QStringLiteral("digest");
    if (!reason.isEmpty()) {
        QFile::remove(part);
        if (canceled) {
            log::info(lcUi, "update.download_canceled");
            return;
        }
        log::warn(lcUi, "update.download_failed",
                  {{"reason", reason},
                   {"received_kb", m_received / 1024},
                   log::fieldIf(reply->error() != QNetworkReply::NoError, "err",
                                log::message(reply->errorString()))});
        setDownload(DownloadFailed);
        return;
    }
    const QString path = QFileInfo(part).absolutePath() + u'/' + asset.name;
    QFile::remove(path);
    if (!QFile::rename(part, path)) {
        QFile::remove(part);
        log::warn(lcUi, "update.download_failed", {"reason", QStringLiteral("file")});
        setDownload(DownloadFailed);
        return;
    }
    m_installer = path;
    log::info(lcUi, "update.downloaded", {"ms", m_downloadClock.elapsed()});
    setDownload(Downloaded);
    emit downloaded(noticed("update.downloaded", tr("Cirrus %1 已下载").arg(m_release.version)));
}

void UpdateCheck::cancelDownload()
{
    if (!m_downloadReply)
        return;
    // Downloading → nothing: finished() sees NoDownload and tells no failure.
    setDownload(NoDownload);
    m_downloadReply->abort();
}

void UpdateCheck::installUpdate()
{
    if (m_download != Downloaded || !QFileInfo::exists(m_installer))
        return;
    m_installerArmed = m_installer;
    log::info(lcUi, "update.install_requested");
    emit installRequested();
}

bool UpdateCheck::runInstaller()
{
    if (m_installerArmed.isEmpty())
        return false;
#ifdef Q_OS_WIN
    // Silent (a progress window, no questions), closing what still holds the files, and
    // opening Cirrus again when done (/RELAUNCH, packaging/windows/cirrus.iss). Where Cirrus
    // is and for whom stay as they were: the uninstall key shows which install this is.
    QStringList arguments{QStringLiteral("/SILENT"), QStringLiteral("/SUPPRESSMSGBOXES"),
                          QStringLiteral("/NORESTART"), QStringLiteral("/CLOSEAPPLICATIONS"),
                          QStringLiteral("/RELAUNCH=1")};
    const QString key = QStringLiteral("\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
                                       "{6E0C2B7A-5D3F-4C1E-9A8B-2F4D7C9E1A35}_is1");
    const QString location = QStringLiteral("InstallLocation");
    if (QSettings(QStringLiteral("HKEY_CURRENT_USER") + key, QSettings::NativeFormat)
            .contains(location))
        arguments << QStringLiteral("/CURRENTUSER");
    else if (QSettings(QStringLiteral("HKEY_LOCAL_MACHINE") + key, QSettings::Registry64Format)
                 .contains(location))
        arguments << QStringLiteral("/ALLUSERS");
    const bool started = QProcess::startDetached(m_installerArmed, arguments);
    log::info(lcUi, "update.installer_started", {{"ok", started}, {"args", arguments.join(u' ')}});
    return started;
#else
    log::warn(lcUi, "update.installer_unsupported");
    return false;
#endif
}

} // namespace cirrus::ui
