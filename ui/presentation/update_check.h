#pragma once

#include "update_release.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace cirrus::ui
{

class AppSettings;

// Whether GitHub has a newer Cirrus (App.updates; docs/tasks/update-check.md). Asks
// GitHub's latest-release API: by hand (check()), and in packaged builds also on its
// own, 30 s after the start and every 24 h after (setting updates/auto), not again
// within 24 h of the last check across restarts. A version found on its own is
// announced once (`notice`); the about settings show the state. openRelease() opens the
// release page in the browser. The Windows installed version also downloads the new
// installer (§9): only the project's release file, checked against GitHub's size and
// sha256; the user starts it (installUpdate()), and it runs once the app has quit.
class UpdateCheck : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.updates")
    Q_PROPERTY(int status READ status NOTIFY changed)   // UpdateCheck.Status
    Q_PROPERTY(int failure READ failure NOTIFY changed) // UpdateCheck.Failure
    Q_PROPERTY(int install READ install CONSTANT)       // UpdateCheck.Install
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString assetName READ assetName NOTIFY changed)
    // Milliseconds since the epoch, 0 when unknown (for App.format).
    Q_PROPERTY(qint64 publishedAt READ publishedAt NOTIFY changed)
    Q_PROPERTY(qint64 lastCheck READ lastCheck NOTIFY changed)
    // Whether it checks on its own in this build (a packaged one).
    Q_PROPERTY(bool canCheckAutomatically READ canCheckAutomatically CONSTANT)
    // Whether this copy downloads and installs the update itself (Windows installer).
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
    Q_PROPERTY(int download READ download NOTIFY downloadChanged) // UpdateCheck.Download
    // 0–1 while downloading.
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY downloadChanged)

  public:
    enum Status { Idle, Checking, UpToDate, Available, Failed };
    Q_ENUM(Status)
    enum Failure { NoFailure, Network, Limited, Invalid };
    Q_ENUM(Failure)
    // How this copy was installed: what to download for an update.
    enum Install { Development, UnknownPackage, Arch, Debian, Fedora, WindowsSetup, WindowsZip, MacDmg };
    Q_ENUM(Install)
    // The installer's download: not started, under way, downloaded and checked, or not.
    enum Download { NoDownload, Downloading, Downloaded, DownloadFailed };
    Q_ENUM(Download)

    explicit UpdateCheck(AppSettings* settings, QObject* parent = nullptr);

    [[nodiscard]] int status() const { return m_status; }
    [[nodiscard]] int failure() const { return m_failure; }
    [[nodiscard]] int install() const { return m_install; }
    [[nodiscard]] QString latestVersion() const { return m_release.version; }
    [[nodiscard]] QString title() const { return m_release.title; }
    [[nodiscard]] QString notes() const { return m_release.notes; }
    [[nodiscard]] QString assetName() const { return m_release.asset ? m_release.asset->name : QString(); }
    [[nodiscard]] qint64 publishedAt() const;
    [[nodiscard]] qint64 lastCheck() const;
    [[nodiscard]] bool canCheckAutomatically() const;
    [[nodiscard]] bool canInstall() const;
    [[nodiscard]] int download() const { return m_download; }
    [[nodiscard]] qreal downloadProgress() const;

    // The build's package identity and architecture from CMake. Empty package and
    // architecture identify a development build; unknown nonempty values still check
    // releases but never guess an asset. Call once, before the event loop.
    void setPackage(const QString& package, const QString& architecture);

    Q_INVOKABLE void check();
    // Opens the exact matching release asset, or the release page if identity is unknown.
    Q_INVOKABLE bool openDownload() const;
    // The release page in the browser; false when there is none (or not GitHub's).
    Q_INVOKABLE bool openRelease() const;
    // Downloads the new installer (canInstall) to a private folder, again after a failure.
    Q_INVOKABLE void downloadInstaller();
    Q_INVOKABLE void cancelDownload();
    // The downloaded installer is to run once the app has quit: installRequested() asks
    // the app to quit, which then calls runInstaller().
    Q_INVOKABLE void installUpdate();
    // At the very end of the app (after the backend has closed): starts the installer
    // installUpdate() asked for, silent, keeping where and for whom Cirrus is installed, and
    // opening Cirrus again when done. False when none was asked for or it did not start.
    bool runInstaller();

  signals:
    void changed();
    // A newer version found on its own, for the first time (a snackbar).
    void notice(const QString& message);
    void downloadChanged();
    // The installer is downloaded and checked (a snackbar with 安装并重启).
    void downloaded(const QString& message);
    // installUpdate(): quit the app now, so the installer can run.
    void installRequested();

  private:
    void schedule();
    void start(bool manual);
    void finish(QNetworkReply* reply, bool manual);
    void setState(Status status, Failure failure);
    void setDownload(Download download);
    void downloadFinished(QNetworkReply* reply);
    void removeDownloads() const;
    [[nodiscard]] QString downloadFolder() const;

    AppSettings* m_settings;
    Install m_install = Development;
    updates::Architecture m_architecture = updates::Architecture::Unknown;
    Status m_status = Idle;
    Failure m_failure = NoFailure;
    updates::Release m_release;
    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    // The installer's download.
    Download m_download = NoDownload;
    QPointer<QNetworkReply> m_downloadReply;
    QFile* m_downloadFile = nullptr;
    QCryptographicHash m_downloadHash{QCryptographicHash::Sha256};
    qint64 m_received = 0;
    QElapsedTimer m_downloadClock;
    QString m_installer;      // the downloaded and checked file
    QString m_installerArmed; // installUpdate() asked for it
    QTimer m_timer;
    QElapsedTimer m_sinceStart;
};

} // namespace cirrus::ui
