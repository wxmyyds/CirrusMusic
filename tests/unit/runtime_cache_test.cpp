#include "../support/fake_range_http.h"
#include "../support/synthetic_cover.h"
#include "backend_host.h"
#include "infrastructure/cache/resource_cache_codec.h"
#include "infrastructure/cache/resource_cache_store.h"
#include "infrastructure/media/audio_store.h"
#include "infrastructure/storage/private_files.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <future>

#ifdef Q_OS_WIN
#include <qt_windows.h>

#include <aclapi.h>
#include <sddl.h>

#include <QProcess>
#endif

using namespace cirrus;
using namespace cirrus::cache;
using namespace cirrus::domain;
using cirrus::ui::BackendHost;

class RuntimeCacheTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_dir;
    BackendHost::Paths m_paths;
    QString m_dataLocation, m_cacheLocation;
    quint64 m_next = 0;

    CacheJob job(CacheOperation operation, CacheKind kind, const QString& account = "42")
    {
        CacheJob result;
        result.id = ++m_next;
        result.operation = operation;
        result.kind = kind;
        result.source.accountId = account;
        result.key = kind == CacheKind::Cover
                         ? coverCacheKey(QUrl("https://p1.music.126.net/native.jpg"), 256)
                         : QStringLiteral("1");
        result.ttlMs = CacheDayMs;
        result.notAfterMs = QDateTime::currentMSecsSinceEpoch() + result.ttlMs;
        return result;
    }

    static std::optional<CacheResult> submit(ResourceCacheStore& store, CacheJob job)
    {
        QSignalSpy done(&store, &ResourceCacheStore::completed);
        store.submit(std::move(job));
        if (done.isEmpty() && !done.wait(5000))
            return {};
        return qvariant_cast<CacheResult>(done.first().first());
    }

    static void readAudio(const std::shared_ptr<media::AudioStream>& stream,
                          const QByteArray& expected)
    {
        auto read = std::async(std::launch::async, [stream] {
            QByteArray result;
            char buffer[16384];
            while (result.size() < stream->size()) {
                const auto count = stream->read(result.size(), buffer, sizeof buffer);
                if (count <= 0)
                    return QByteArray();
                result.append(buffer, count);
            }
            return result;
        });
        auto cancelOnExit = qScopeGuard([stream] { stream->cancel(); });
        QTRY_VERIFY_WITH_TIMEOUT(
            read.wait_for(std::chrono::seconds(0)) == std::future_status::ready, 5000);
        QCOMPARE(read.get(), expected);
        cancelOnExit.dismiss();
    }

  private slots:
    void init()
    {
        // A private build-tree parent: Unix deliberately rejects shared /tmp ancestors.
        m_dir = std::make_unique<QTemporaryDir>(QDir::currentPath() +
                                                QStringLiteral("/native-cache-XXXXXX"));
        QVERIFY(m_dir->isValid());
        m_dataLocation = m_dir->path() + QStringLiteral("/资料 data");
        m_cacheLocation = m_dir->path() + QStringLiteral("/缓存 cache");
        QString error;
        const auto paths = BackendHost::preparePaths(m_dataLocation, m_cacheLocation, &error);
        QVERIFY2(paths.has_value(), qPrintable(error));
        m_paths = *paths;
    }

    void preparesPrivatePathsAndKeepsThemOnRestart()
    {
        QCOMPARE(m_paths.database, m_dataLocation + QStringLiteral("/Cirrus/state.sqlite"));
        QCOMPARE(m_paths.cache, m_cacheLocation + QStringLiteral("/Cirrus/resources"));
        QCOMPARE(m_paths.audioCache, m_cacheLocation + QStringLiteral("/Cirrus/audio"));
        for (const auto& directory :
             {QFileInfo(m_paths.database).absolutePath(), m_paths.cache, m_paths.audioCache})
            QVERIFY2(storage::isPrivateDirectory(directory), storage::lastRefusal().reason);

        auto marker = storage::openPrivateFile(m_paths.audioCache + QStringLiteral("/kept"));
        QVERIFY(marker);
        QCOMPARE(marker->write("kept"), qint64(4));
        marker.reset();
        QString error;
        const auto reopened = BackendHost::preparePaths(m_dataLocation, m_cacheLocation, &error);
        QVERIFY2(reopened.has_value(), qPrintable(error));
        auto read = storage::openPrivateFile(reopened->audioCache + QStringLiteral("/kept"),
                                             QIODevice::ReadOnly);
        QVERIFY(read);
        QCOMPARE(read->readAll(), QByteArray("kept"));
    }

    void rejectsMissingRelativeAndUncleanLocations()
    {
        for (const auto& invalid : {QString(), QStringLiteral("relative/cache"),
                                    m_dir->path() + QStringLiteral("/../cache")}) {
            QString error;
            QVERIFY(!BackendHost::preparePaths(invalid, m_cacheLocation, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(!BackendHost::preparePaths(m_dataLocation, invalid, &error));
            QVERIFY(!error.isEmpty());
        }
    }

    void rejectsUnsafeExistingCacheDirectory()
    {
#ifdef Q_OS_WIN
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        QVERIFY(ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;FA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr));
        const auto release = qScopeGuard([&] { LocalFree(descriptor); });
        BOOL present = FALSE, defaulted = FALSE;
        PACL acl = nullptr;
        QVERIFY(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted));
        QVERIFY(present && acl);
        auto native = QDir::toNativeSeparators(m_paths.cache).toStdWString();
        QCOMPARE(
            SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, acl, nullptr),
            DWORD(ERROR_SUCCESS));
#else
        QVERIFY(QFile::setPermissions(m_paths.cache,
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                          QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                          QFileDevice::ExeOther));
#endif
        QVERIFY(!storage::isPrivateDirectory(m_paths.cache));
        QString error;
        QVERIFY(!BackendHost::preparePaths(m_dataLocation, m_cacheLocation, &error));
        QCOMPARE(error, m_paths.cache);
        QVERIFY(!storage::isPrivateDirectory(m_paths.cache)); // not silently repaired
    }

    void rejectsLinkedCacheDirectory()
    {
        const auto target = m_dir->path() + QStringLiteral("/link target");
        QVERIFY(QDir().mkpath(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner));
        QVERIFY(QDir().rmdir(m_paths.cache));
#ifdef Q_OS_WIN
        QProcess link;
        link.setProgram(QStringLiteral("cmd.exe"));
        link.setNativeArguments(
            QStringLiteral("/d /c mklink /J \"%1\" \"%2\"")
                .arg(QDir::toNativeSeparators(m_paths.cache), QDir::toNativeSeparators(target)));
        link.start();
        QVERIFY(link.waitForFinished(5000));
        QCOMPARE(link.exitCode(), 0);
        const auto removeLink = qScopeGuard([&] { QDir().rmdir(m_paths.cache); });
#else
        QVERIFY(QFile::link(target, m_paths.cache));
#endif
        QString error;
        QVERIFY(!BackendHost::preparePaths(m_dataLocation, m_cacheLocation, &error));
        QCOMPARE(error, m_paths.cache);
        QVERIFY(storage::isPrivateDirectory(target));
    }

    void resourcesSurviveRestartAndStayAccountScoped()
    {
        auto state = storage::openPrivateFile(m_paths.database);
        QVERIFY(state);
        QCOMPARE(state->write("state stays"), qint64(11));
        state.reset();
        storage::DatabaseWorker database(m_paths.database, {});

        Song song;
        song.id = {QStringLiteral("1")};
        song.title = QStringLiteral("歌曲 title");
        song.durationMs = 120000;
        SongLyrics lyrics;
        lyrics.song = song.id;
        lyrics.original.standard.availability = LyricAvailability::Present;
        lyrics.original.standard.timing = LyricTiming::Lines;
        lyrics.original.standard.lines.append({0, 1000, QStringLiteral("歌词 line"), {}});
        const auto cover = test::syntheticCover();
        QVERIFY(!cover.isEmpty());
        const QList<std::pair<CacheKind, QByteArray>> values{
            {CacheKind::Song, encodeCacheValue(song, CacheKind::Song)},
            {CacheKind::Lyrics, encodeCacheValue(lyrics, CacheKind::Lyrics)},
            {CacheKind::Cover, cover}};
        {
            ResourceCacheStore store(database, m_paths.cache);
            for (const auto& [kind, payload] : values) {
                QVERIFY(!payload.isEmpty());
                auto write = job(CacheOperation::Write, kind);
                write.payload = payload;
                const auto result = submit(store, std::move(write));
                QVERIFY(result.has_value());
                QCOMPARE(result->error, CacheError::None);
                QCOMPARE(result->status, CacheStatus::Stored);
                QVERIFY(result->persisted);
            }
        }
        {
            ResourceCacheStore store(database, m_paths.cache);
            for (const auto& [kind, payload] : values) {
                const auto hit = submit(store, job(CacheOperation::Read, kind));
                QVERIFY(hit.has_value());
                QCOMPARE(hit->error, CacheError::None);
                QCOMPARE(hit->status, CacheStatus::Hit);
                if (kind == CacheKind::Cover)
                    QCOMPARE(std::get<QByteArray>(hit->value), payload);
                else
                    QCOMPARE(encodeCacheValue(hit->value, kind), payload);
                const auto other = submit(store, job(CacheOperation::Read, kind, "43"));
                QVERIFY(other.has_value());
                QCOMPARE(other->error, CacheError::None);
                QCOMPARE(other->status, CacheStatus::Miss);
            }
            const auto clear = submit(store, job(CacheOperation::ClearAll, CacheKind::Song));
            QVERIFY(clear.has_value());
            QCOMPARE(clear->error, CacheError::None);
            const auto usage = submit(store, job(CacheOperation::Usage, CacheKind::Song));
            QVERIFY(usage.has_value());
            QCOMPARE(usage->error, CacheError::None);
            QCOMPARE(usage->usage.entries, qint64(0));
        }
        state = storage::openPrivateFile(m_paths.database, QIODevice::ReadOnly);
        QVERIFY(state);
        QCOMPARE(state->readAll(), QByteArray("state stays"));
    }

    void audioSurvivesRestartAndClearsAfterTheReaderCloses()
    {
        test::RangeServer server{QByteArray(256 * 1024 + 71, 'a')};
        MediaSource source{QUrl("https://m801.music.126.net/native.mp3"), "exhigh", "mp3",
                           server.body.size(), QDeadlineTimer(60000)};
        source.identity =
            ContentHash{QCryptographicHash::hash(server.body, QCryptographicHash::Md5).toHex()};
        const auto createStore = [&] {
            media::AudioStore::Options options;
            options.root = m_paths.audioCache;
            auto store = std::make_unique<media::AudioStore>(
                options, std::make_unique<test::RangeManager>(server));
            store->setPartition(QStringLiteral("account-a"));
            return store;
        };
        {
            auto store = createStore();
            QVERIFY(store->persistent());
            auto stream = store->open(source);
            QVERIFY(stream);
            readAudio(stream, server.body);
            if (QTest::currentTestFailed())
                return;
            QTRY_COMPARE_WITH_TIMEOUT(store->usage(), server.body.size(), 5000);
            store->keepAnalysis(source, "analysis");
            QCOMPARE(store->analysis(source), QByteArray("analysis"));
        }
        int files = 0;
        QDirIterator entries(m_paths.audioCache, QDir::Files, QDirIterator::Subdirectories);
        while (entries.hasNext()) {
            const auto path = entries.next();
            QVERIFY2(storage::isPrivateFileOrAbsent(path), storage::lastRefusal().reason);
            ++files;
        }
        QCOMPARE(files, 3); // audio, progress metadata, and analysis, all retained
        server.ranges.clear();
        auto store = createStore();
        QVERIFY(store->persistent());
        auto stream = store->open(source);
        QVERIFY(stream);
        readAudio(stream, server.body);
        if (QTest::currentTestFailed())
            return;
        QVERIFY(server.ranges.isEmpty()); // a new store uses disk, with no CDN request
        QCOMPARE(store->analysis(source), QByteArray("analysis"));
        store->clear(true);
        readAudio(stream, server.body); // clearing does not break an active reader
        if (QTest::currentTestFailed())
            return;
        stream.reset();
        QTRY_COMPARE_WITH_TIMEOUT(store->usage(), 0, 5000);
        QVERIFY(store->analysis(source).isEmpty());
    }
};

QTEST_GUILESS_MAIN(RuntimeCacheTest)
#include "runtime_cache_test.moc"
