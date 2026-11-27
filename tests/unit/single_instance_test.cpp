#include "single_instance.h"

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSignalSpy>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>

using cirrus::ui::SingleInstance;

class SingleInstanceTest : public QObject
{
    Q_OBJECT
  private slots:
    void heldLockWithoutSocketIsAnError()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString lockPath = directory.filePath(QStringLiteral("instance.lock"));
        QLockFile owner(lockPath);
        owner.setStaleLockTime(0);
        QVERIFY(owner.tryLock());

        SingleInstance contender(directory.path());
        QCOMPARE(contender.claim(), SingleInstance::ClaimResult::Failed);

        QLockFile anotherContender(lockPath);
        anotherContender.setStaleLockTime(0);
        QVERIFY(!anotherContender.tryLock(0));
        QVERIFY(owner.isLocked());
    }

    void listenFailureReleasesTheLock()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("instance.socket"))));

        SingleInstance instance(directory.path());
        QCOMPARE(instance.claim(), SingleInstance::ClaimResult::Failed);

        QLockFile lock(directory.filePath(QStringLiteral("instance.lock")));
        lock.setStaleLockTime(0);
        QVERIFY(lock.tryLock(0));
    }

    void recoversAStaleLockAndSocketAndActivatesTheOwner()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString lockPath = directory.filePath(QStringLiteral("instance.lock"));
        QFile staleLock(lockPath);
        QVERIFY(staleLock.open(QIODevice::WriteOnly));
        const QString hostName = QSysInfo::machineHostName();
        const QString applicationName = QCoreApplication::applicationName();
        const QByteArray lockContents = QByteArrayLiteral("2147483647\n") +
                                        applicationName.toUtf8() + '\n' + hostName.toUtf8() + '\n';
        QCOMPARE(staleLock.write(lockContents), qint64(lockContents.size()));
        staleLock.close();

        QLockFile lockInfo(lockPath);
        qint64 lockPid = 0;
        QString lockHostName;
        QString lockApplicationName;
        QVERIFY(lockInfo.getLockInfo(&lockPid, &lockHostName, &lockApplicationName));
        QCOMPARE(lockPid, qint64(2147483647));
        QCOMPARE(lockHostName, hostName);
        QCOMPARE(lockApplicationName, applicationName);

        const QString socketPath = directory.filePath(QStringLiteral("instance.socket"));
        QFile staleSocket(socketPath);
        QVERIFY(staleSocket.open(QIODevice::WriteOnly));
        QCOMPARE(staleSocket.write(QByteArrayLiteral("stale")), qint64(5));
        staleSocket.close();

        SingleInstance owner(directory.path());
        QCOMPARE(owner.claim(), SingleInstance::ClaimResult::Started);
        QSignalSpy activated(&owner, &SingleInstance::activateRequested);
        QVERIFY(activated.isValid());

        SingleInstance second(directory.path());
        QCOMPARE(second.claim(), SingleInstance::ClaimResult::Activated);
        QTRY_COMPARE(activated.size(), 1);
    }
};

QTEST_GUILESS_MAIN(SingleInstanceTest)
#include "single_instance_test.moc"
