#include "infrastructure/storage/database_worker.h"
#include "infrastructure/storage/installation_store.h"
#include "infrastructure/storage/private_files.h"

#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace cirrus::storage;

class StorageTest : public QObject
{
    Q_OBJECT
  private slots:
    void freshDatabase();
    void privateFileReadModesAndDeleteSharing();
    void lockedByAnotherWorker();
    void rejectsEditedMigration();
    void rejectsNewerSchema();
    void upgradeBacksUpAndFailedStepRollsBack();
    void corruptFile();
    void jobsAndCallbacks();
    void deviceIdGeneratedOnceAndStable();
    void invalidStoredDeviceIdNotRegenerated();
};

namespace
{
const Migration V1{1, {QStringLiteral("CREATE TABLE item (id INTEGER PRIMARY KEY, name TEXT)")}};
const Migration V2{2, {QStringLiteral("ALTER TABLE item ADD COLUMN extra TEXT")}};

StorageError openWorker(DatabaseWorker& worker)
{
    QSignalSpy spy(&worker, &DatabaseWorker::opened);
    worker.open();
    if (!spy.wait(5000)) {
        return StorageError::Unavailable;
    }
    return qvariant_cast<StorageError>(spy.first().first());
}

template <typename T>
StorageResult<T> runSync(DatabaseWorker& worker, std::function<StorageResult<T>(QSqlDatabase&)> job)
{
    QObject context;
    std::optional<StorageResult<T>> result;
    worker.run<T>(&context, std::move(job), [&result](StorageResult<T> r) { result = r; });
    if (!QTest::qWaitFor([&result] { return result.has_value(); }, 5000)) {
        return {StorageError::Query, {}};
    }
    return *result;
}

StorageResult<QString> query(DatabaseWorker& worker, const QString& sql)
{
    return runSync<QString>(worker, [sql](QSqlDatabase& db) -> StorageResult<QString> {
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            return {StorageError::Query, {}};
        }
        return {StorageError::None, q.next() ? q.value(0).toString() : QString()};
    });
}
} // namespace

void StorageTest::freshDatabase()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("nested/state.sqlite"));
    {
        DatabaseWorker worker(path, {V1});
        QCOMPARE(openWorker(worker), StorageError::None);
#ifdef Q_OS_WIN
        // Check ownership and the native DACL policy; Windows permission bits
        // are not POSIX mode bits (and Qt disables NTFS ACL lookup by default).
        QVERIFY(QFileInfo::exists(path));
        QVERIFY2(isPrivateFileOrAbsent(path), lastRefusal().reason);
        QVERIFY2(isPrivateDirectory(dir.filePath(QStringLiteral("nested"))), lastRefusal().reason);
#else
        // Unix mode bits (user/group/other) as seen by the file owner: 0600 and 0700.
        QCOMPARE(QFile::permissions(path) & 0x0FFF, QFileDevice::ReadUser | QFileDevice::WriteUser);
        QCOMPARE(QFile::permissions(dir.filePath(QStringLiteral("nested"))) & 0x0FFF,
                 QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);
#endif
        QCOMPARE(query(worker, QStringLiteral("PRAGMA foreign_keys")).value, QStringLiteral("1"));
        QCOMPARE(query(worker, QStringLiteral("PRAGMA synchronous")).value, QStringLiteral("2"));
        QCOMPARE(query(worker, QStringLiteral("PRAGMA journal_mode")).value, QStringLiteral("wal"));
        QCOMPARE(query(worker, QStringLiteral("SELECT count(*) FROM schema_migrations")).value,
                 QStringLiteral("1"));
        QVERIFY(QFile::exists(path + QStringLiteral(".lock")));
    }
    QVERIFY(!QFile::exists(path + QStringLiteral(".lock"))); // released on destruction
    DatabaseWorker reopened(path, {V1});
    QCOMPARE(openWorker(reopened), StorageError::None);
    QCOMPARE(query(reopened, QStringLiteral("SELECT count(*) FROM schema_migrations")).value,
             QStringLiteral("1"));
}

void StorageTest::privateFileReadModesAndDeleteSharing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("shared.audio"));
    QVERIFY(!openPrivateFile(path, QIODevice::ReadOnly));
    QVERIFY(!QFileInfo::exists(path)); // A reader must never create an empty cache entry.
    for (const auto mode :
         QList<QIODevice::OpenMode>{QIODevice::WriteOnly, QIODevice::ReadWrite | QIODevice::Append,
                                    QIODevice::ReadWrite | QIODevice::Truncate}) {
        QVERIFY(!openPrivateFile(path, mode));
        QVERIFY(!QFileInfo::exists(path));
    }
    auto writer = openPrivateFile(path);
    QVERIFY(writer != nullptr);
    QCOMPARE(writer->write("data"), qint64(4));
    QVERIFY(writer->flush());
    auto reader = openPrivateFile(path, QIODevice::ReadOnly | QIODevice::Unbuffered);
    QVERIFY(reader != nullptr);
    QVERIFY(reader->isReadable());
    QVERIFY(!reader->isWritable());
    QVERIFY(reader->openMode().testFlag(QIODevice::Unbuffered));
    QCOMPARE(reader->readAll(), QByteArray("data"));
    QVERIFY(writer->seek(0));
    QCOMPARE(writer->write("next"), qint64(4));
    QVERIFY(writer->flush());
    QVERIFY(reader->seek(0));
    QCOMPARE(reader->readAll(), QByteArray("next"));
    // Invalidation can unlink while readers/writers retain their own handles,
    // including Windows: neither handle may deny delete sharing.
    QVERIFY(QFile::remove(path));
    QVERIFY(reader->seek(0));
    QCOMPARE(reader->readAll(), QByteArray("next"));
    reader.reset();
    writer.reset();
    QVERIFY(!QFileInfo::exists(path));
}

void StorageTest::lockedByAnotherWorker()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("state.sqlite"));
    DatabaseWorker first(path, {V1});
    QCOMPARE(openWorker(first), StorageError::None);
    DatabaseWorker second(path, {V1});
    QCOMPARE(openWorker(second), StorageError::Locked);
    QCOMPARE(query(second, QStringLiteral("SELECT 1")).error, StorageError::NotOpen);
}

void StorageTest::rejectsEditedMigration()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("state.sqlite"));
    {
        DatabaseWorker worker(path, {V1});
        QCOMPARE(openWorker(worker), StorageError::None);
    }
    auto edited = V1;
    edited.statements[0].append(QStringLiteral(" "));
    DatabaseWorker worker(path, {edited});
    QCOMPARE(openWorker(worker), StorageError::ChecksumMismatch);
}

void StorageTest::rejectsNewerSchema()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("state.sqlite"));
    {
        DatabaseWorker worker(path, {V1, V2});
        QCOMPARE(openWorker(worker), StorageError::None);
    }
    DatabaseWorker older(path, {V1});
    QCOMPARE(openWorker(older), StorageError::NewerSchema);
}

void StorageTest::upgradeBacksUpAndFailedStepRollsBack()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("state.sqlite"));
    {
        DatabaseWorker worker(path, {V1});
        QCOMPARE(openWorker(worker), StorageError::None);
        QCOMPARE(query(worker, QStringLiteral("INSERT INTO item (name) VALUES ('kept')")).error,
                 StorageError::None);
    }
    {
        DatabaseWorker worker(path, {V1, V2});
        QCOMPARE(openWorker(worker), StorageError::None);
        QCOMPARE(query(worker, QStringLiteral("SELECT name FROM item")).value,
                 QStringLiteral("kept"));
    }
    QVERIFY(QFile::exists(path + QStringLiteral(".backup-v1")));
    {
        DatabaseWorker backup(path + QStringLiteral(".backup-v1"), {V1});
        QCOMPARE(openWorker(backup), StorageError::None); // a valid, consistent v1 snapshot
        QCOMPARE(query(backup, QStringLiteral("SELECT name FROM item")).value,
                 QStringLiteral("kept"));
    }
    const Migration broken{
        3, {QStringLiteral("CREATE TABLE half (id INTEGER)"), QStringLiteral("THIS IS NOT SQL")}};
    {
        DatabaseWorker worker(path, {V1, V2, broken});
        QCOMPARE(openWorker(worker), StorageError::MigrationFailed);
    }
    DatabaseWorker worker(path, {V1, V2});
    QCOMPARE(openWorker(worker), StorageError::None); // version still 2, nothing half-applied
    QCOMPARE(query(worker, QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = 'half'"))
                 .value,
             QStringLiteral("0"));
}

void StorageTest::corruptFile()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("state.sqlite"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(4096, 'x'));
    file.close();
    DatabaseWorker worker(path, {V1});
    QCOMPARE(openWorker(worker), StorageError::Corrupt);
    QCOMPARE(file.size(), 4096); // untouched
}

void StorageTest::jobsAndCallbacks()
{
    QTemporaryDir dir;
    DatabaseWorker worker(dir.filePath(QStringLiteral("state.sqlite")), {V1});
    // Before open: callback receives NotOpen without running the job.
    bool ran = false;
    QCOMPARE(runSync<int>(worker,
                          [&ran](QSqlDatabase&) -> StorageResult<int> {
                              ran = true;
                              return {};
                          })
                 .error,
             StorageError::NotOpen);
    QVERIFY(!ran);
    QCOMPARE(openWorker(worker), StorageError::None);

    QThread* jobThread = nullptr;
    QThread* callbackThread = nullptr;
    auto context = std::make_unique<QObject>();
    bool delivered = false;
    worker.run<int>(
        context.get(),
        [&jobThread](QSqlDatabase&) -> StorageResult<int> {
            jobThread = QThread::currentThread();
            return {StorageError::None, 7};
        },
        [&](StorageResult<int> result) {
            callbackThread = QThread::currentThread();
            delivered = result.value == 7;
        });
    QTRY_VERIFY(delivered);
    QVERIFY(jobThread && jobThread != QThread::currentThread());
    QCOMPARE(callbackThread, QThread::currentThread());

    // A destroyed context never receives its callback, even though the job runs.
    bool late = false;
    bool executed = false;
    worker.run<int>(
        context.get(),
        [&executed](QSqlDatabase&) -> StorageResult<int> {
            executed = true;
            return {};
        },
        [&late](StorageResult<int>) { late = true; });
    context.reset();
    QTRY_VERIFY(executed);
    QTest::qWait(20);
    QVERIFY(!late);
}

void StorageTest::deviceIdGeneratedOnceAndStable()
{
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("state.sqlite"));
    int draws = 0;
    const auto random = [&draws](qsizetype size) -> std::optional<QByteArray> {
        ++draws;
        QByteArray bytes(size, Qt::Uninitialized);
        for (qsizetype i = 0; i < size; ++i) {
            bytes[i] = static_cast<char>(0xA0 + i);
        }
        return bytes;
    };
    const QString expected = QStringLiteral("A0A1A2A3A4A5A6A7A8A9AAABACADAEAFB0B1B2B3B4B5B6B7B8B9");
    for (int run = 0; run < 2; ++run) {
        DatabaseWorker worker(path, InstallationStore::migrations());
        QCOMPARE(openWorker(worker), StorageError::None);
        InstallationStore store(worker, random);
        QObject context;
        std::optional<StorageResult<QString>> result;
        store.deviceId(&context, [&result](StorageResult<QString> r) { result = r; });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->error, StorageError::None);
        QCOMPARE(result->value, expected);
    }
    QCOMPARE(draws, 1); // generated once, then read back

    // Production randomness: 52 upper-case hex characters.
    QTemporaryDir other;
    DatabaseWorker worker(other.filePath(QStringLiteral("state.sqlite")),
                          InstallationStore::migrations());
    QCOMPARE(openWorker(worker), StorageError::None);
    InstallationStore store(worker);
    QObject context;
    std::optional<StorageResult<QString>> result;
    store.deviceId(&context, [&result](StorageResult<QString> r) { result = r; });
    QTRY_VERIFY(result.has_value());
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9A-F]{52}$")).match(result->value).hasMatch());
    QVERIFY(result->value != expected);
}

void StorageTest::invalidStoredDeviceIdNotRegenerated()
{
    QTemporaryDir dir;
    DatabaseWorker worker(dir.filePath(QStringLiteral("state.sqlite")),
                          InstallationStore::migrations());
    QCOMPARE(openWorker(worker), StorageError::None);
    QCOMPARE(
        query(worker, QStringLiteral("INSERT INTO installation VALUES (1, 'bad id', 0)")).error,
        StorageError::None);
    InstallationStore store(worker);
    QObject context;
    std::optional<StorageResult<QString>> result;
    store.deviceId(&context, [&result](StorageResult<QString> r) { result = r; });
    QTRY_VERIFY(result.has_value());
    QCOMPARE(result->error, StorageError::Corrupt);
    QCOMPARE(query(worker, QStringLiteral("SELECT device_id FROM installation")).value,
             QStringLiteral("bad id"));
}

QTEST_GUILESS_MAIN(StorageTest)
#include "storage_test.moc"
