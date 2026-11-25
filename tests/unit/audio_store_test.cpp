#include "../support/fake_range_http.h"
#include "infrastructure/media/audio_store.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <future>
using namespace cirrus;
using namespace domain;
using media::AudioStore;
using media::AudioStream;
namespace
{
constexpr qint64 Chunk = 256 * 1024;

QByteArray noise(qint64 bytes, quint32 seed)
{
    QByteArray data(bytes, Qt::Uninitialized);
    QRandomGenerator random(seed);
    random.fillRange(reinterpret_cast<quint32*>(data.data()), bytes / 4);
    return data;
}
MediaSource source(const QByteArray& body, bool withMd5 = true, const char* name = "a.mp3")
{
    MediaSource s{QUrl(QStringLiteral("https://m801.music.126.net/") + QLatin1String(name)),
                  "exhigh", "mp3", body.size(), QDeadlineTimer(60000)};
    if (withMd5)
        s.identity = ContentHash{QCryptographicHash::hash(body, QCryptographicHash::Md5).toHex()};
    return s;
}
// A Bilibili stream-addressed source: no digest, a stream name, its CDN, its headers.
MediaSource biliSource(const QByteArray& body,
                       const QString& key = QStringLiteral("bili-auto-BV1-1-id-30280"))
{
    MediaSource s{QUrl(QStringLiteral("https://upos-sz-mirrorcos.bilivideo.com/a.m4s")), "hires",
                  "aac", body.size(), QDeadlineTimer(60000)};
    s.provider = SourceProvider::Bilibili;
    s.identity = StreamKey{key};
    s.referer = QStringLiteral("https://www.bilibili.com/");
    s.userAgent = QStringLiteral("Mozilla/5.0 CirrusTest");
    return s;
}
// Reads on another thread (reads block) while the test spins the event loop.
std::future<QByteArray> readAsync(std::shared_ptr<AudioStream> stream, qint64 from = 0,
                                  qint64 bytes = -1)
{
    return std::async(std::launch::async, [stream, from, bytes] {
        QByteArray out;
        const qint64 end = bytes < 0 ? stream->size() : from + bytes;
        char buffer[48 * 1024];
        for (qint64 at = from; at < end;) {
            const auto got = stream->read(at, buffer, std::min<qint64>(sizeof buffer, end - at));
            if (got <= 0)
                return got < 0 ? QByteArray("FAILED") : out;
            out.append(buffer, got);
            at += got;
        }
        return out;
    });
}
bool ready(std::future<QByteArray>& f)
{
    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
} // namespace

class AudioStoreTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_root;

    std::unique_ptr<AudioStore> store(test::RangeServer& server, qint64 capacity = 64 << 20)
    {
        AudioStore::Options options;
        options.root = m_root;
        options.capacityBytes = capacity;
        options.retryDelayMs = 10;
        options.refreshTimeoutMs = 300;
        auto s =
            std::make_unique<AudioStore>(options, std::make_unique<test::RangeManager>(server));
        s->setPartition(QStringLiteral("account-a"));
        return s;
    }
    // Waits for the stream's file to be verified and the handle released.
    static void settle(AudioStore& store, qint64 usage)
    {
        QTRY_COMPARE_WITH_TIMEOUT(store.usage(), usage, 5000);
    }
    // The data files the store keeps, in any partition.
    int dataFiles() const
    {
        int count = 0;
        QDirIterator it(m_root, {QStringLiteral("*.audio")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            ++count;
        }
        return count;
    }

  private slots:
    void init()
    {
        // Under the build tree: /tmp fails the private-path check (world-writable).
        m_dir = std::make_unique<QTemporaryDir>(QCoreApplication::applicationDirPath() +
                                                QStringLiteral("/audio-store-XXXXXX"));
        QVERIFY(m_dir->isValid());
        m_root = m_dir->path() + QStringLiteral("/audio");
    }
    void cleanup() { m_dir.reset(); }

    // The reader catches up with the download in small reads (a decoder near the write
    // head): what it gets is what arrived, never the unwritten (zero) bytes after it,
    // whatever its file handle read ahead (2026-11-09: FLAC failed to decode at start).
    void readsAtTheDownloadHeadSeeNoUnwrittenBytes()
    {
        test::RangeServer server{noise(300 * 1024, 7)};
        server.blockBytes = 3000;
        server.intervalMs = 1;
        auto s = store(server);
        auto stream = s->open(source(server.body));
        QVERIFY(stream);
        auto read = std::async(std::launch::async, [stream] {
            QByteArray out;
            char buffer[2048];
            while (out.size() < stream->size()) {
                const auto got = stream->read(out.size(), buffer, sizeof buffer);
                if (got <= 0)
                    return QByteArray("FAILED");
                out.append(buffer, got);
            }
            return out;
        });
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 10000);
        const QByteArray got = read.get();
        QCOMPARE(got.size(), server.body.size());
        QVERIFY(got == server.body);
    }

    void streamsThenPlaysFromCache()
    {
        test::RangeServer server{noise(Chunk * 4 + 1000, 1)};
        auto s = store(server);
        QVERIFY(s->persistent());
        auto stream = s->open(source(server.body));
        QVERIFY(stream);
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        QTRY_VERIFY_WITH_TIMEOUT(stream->complete(), 5000);
        stream.reset();
        settle(*s, server.body.size());
        const auto requests = server.ranges.size();

        // Again: from the disk, no request.
        auto again = s->open(source(server.body));
        auto second = readAsync(again);
        QTRY_VERIFY_WITH_TIMEOUT(ready(second), 5000);
        QCOMPARE(second.get(), server.body);
        QCOMPARE(server.ranges.size(), requests);
    }

    void seekFetchesThereFirst()
    {
        test::RangeServer server{noise(Chunk * 16, 2)};
        server.intervalMs = 5;
        server.blockBytes = 32 * 1024;
        auto s = store(server);
        auto stream = s->open(source(server.body));
        const qint64 at = Chunk * 12 + 100;
        auto read = readAsync(stream, at, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body.mid(at, 10000));
        QVERIFY(server.ranges.contains("bytes=" + QByteArray::number(Chunk * 12) + '-' +
                                       QByteArray::number(server.body.size() - 1)));
        // The rest fills in behind, and the whole file checks out.
        QTRY_VERIFY_WITH_TIMEOUT(stream->complete(), 10000);
        auto all = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(all), 5000);
        QCOMPARE(all.get(), server.body);
        stream.reset();
        settle(*s, server.body.size());
    }

    void serverIgnoringRangeStillWorks()
    {
        test::RangeServer server{noise(Chunk * 6, 3)};
        server.honorRange = false;
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream, Chunk * 4, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body.mid(Chunk * 4, 5000));
    }

    void wrongContentRangeFails()
    {
        test::RangeServer server{noise(Chunk * 2, 4)};
        server.contentRange = "bytes 0-99/99999";
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), QByteArray("FAILED"));
        QCOMPARE(stream->failure(), std::optional(PlaybackError::InvalidResponse));
    }

    void refusedAddressAsksForANewOne()
    {
        test::RangeServer server{noise(Chunk * 3, 5)};
        server.statuses = {403};
        auto s = store(server);
        QSignalSpy needed(s.get(), &AudioStore::urlNeeded);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_COMPARE_WITH_TIMEOUT(needed.size(), 1, 5000);
        s->refresh(needed.first().first().toULongLong(),
                   QUrl(QStringLiteral("https://m802.music.126.net/fresh.mp3")),
                   QDeadlineTimer(60000));
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        QCOMPARE(server.urls.last().host(), QStringLiteral("m802.music.126.net"));
    }

    void noNewAddressExpires()
    {
        test::RangeServer server{noise(Chunk * 3, 6)};
        server.statuses = {403};
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), QByteArray("FAILED"));
        QCOMPARE(stream->failure(), std::optional(PlaybackError::Expired));
    }

    void retriesThenGivesUp()
    {
        test::RangeServer server{noise(Chunk * 2, 7)};
        server.statuses = {500, 500};
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);

        test::RangeServer down{noise(Chunk * 2, 8)};
        down.statuses = {500, 500, 500, 500};
        auto t = store(down);
        auto failing = t->open(source(down.body));
        auto lost = readAsync(failing);
        QTRY_VERIFY_WITH_TIMEOUT(ready(lost), 5000);
        QCOMPARE(lost.get(), QByteArray("FAILED"));
        QCOMPARE(failing->failure(), std::optional(PlaybackError::Network));
    }

    void wrongMd5IsNotKept()
    {
        test::RangeServer server{noise(Chunk * 2, 9)};
        auto s = store(server);
        auto bad = source(server.body);
        bad.identity = ContentHash{QByteArray(32, 'a')};
        auto stream = s->open(bad);
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body); // still plays
        stream.reset();
        // The md5 check runs on a pool thread after the last byte; until it has removed the
        // file, the next open() shares this download. usage() is no sign of it: a file this
        // small has saved no progress yet, so it counts nothing from the start.
        QTRY_COMPARE_WITH_TIMEOUT(dataFiles(), 0, 5000);
        QCOMPARE(s->usage(), 0);
        const auto requests = server.ranges.size();
        auto again = s->open(bad);
        auto second = readAsync(again);
        QTRY_VERIFY_WITH_TIMEOUT(ready(second), 5000);
        QVERIFY(server.ranges.size() > requests); // downloaded again
    }

    void evictsTheLeastRecentlyUsed()
    {
        test::RangeServer server{noise(Chunk * 4, 10)};
        auto s = store(server, Chunk * 10); // room for two of three files
        QList<MediaSource> sources;
        for (int i = 0; i < 3; ++i) {
            server.body = noise(Chunk * 4, 20 + i);
            sources.append(source(server.body, true,
                                  i == 0   ? "1.mp3"
                                  : i == 1 ? "2.mp3"
                                           : "3.mp3"));
            auto stream = s->open(sources.last());
            auto read = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(stream->complete(), 5000);
            stream.reset();
            QTest::qWait(20); // distinct last-used times
            QTRY_VERIFY_WITH_TIMEOUT(s->usage() % (Chunk * 4) == 0, 5000);
        }
        QTRY_COMPARE_WITH_TIMEOUT(s->usage(), Chunk * 8, 5000);
        // The first one went: opening it downloads again.
        const auto requests = server.ranges.size();
        server.body = noise(Chunk * 4, 20);
        auto first = s->open(sources.first());
        auto read = readAsync(first);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QVERIFY(server.ranges.size() > requests);
    }

    void loweringTheCapacityEvictsNowButKeepsOpenFiles()
    {
        test::RangeServer server{noise(Chunk * 4, 30)};
        auto s = store(server);
        std::shared_ptr<AudioStream> open;
        for (int i = 0; i < 3; ++i) {
            server.body = noise(Chunk * 4, 31 + i);
            auto stream = s->open(source(server.body, true,
                                         i == 0   ? "1.mp3"
                                         : i == 1 ? "2.mp3"
                                                  : "3.mp3"));
            auto read = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(stream->complete(), 5000);
            if (i == 0)
                open = stream; // the oldest, still playing
            QTest::qWait(20);
        }
        QTRY_COMPARE_WITH_TIMEOUT(s->usage(), Chunk * 12, 5000);
        // (The last one's download is let go a moment after its stream closes.)
        QTRY_COMPARE_WITH_TIMEOUT((s->setCapacity(Chunk * 4), s->usage()), Chunk * 4, 5000);
        QCOMPARE(s->capacity(), Chunk * 4); // only the open one is left
        QVERIFY(open->complete());
    }

    // An analysis is kept beside its file and goes with it, evicted or cleared; none
    // without a kept file.
    void analysesGoWithTheirFiles()
    {
        test::RangeServer server{noise(Chunk * 2, 40)};
        auto s = store(server);
        const auto a = source(server.body);
        std::shared_ptr<AudioStream> stream;
        const auto download = [&] {
            stream = s->open(a);
            QVERIFY(stream);
            auto read = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(stream->complete(), 5000);
        };
        s->keepAnalysis(a, "early");
        QVERIFY(s->analysis(a).isEmpty()); // no file yet
        download();
        if (QTest::currentTestFailed())
            return;
        s->keepAnalysis(a, "analysis");
        QCOMPARE(s->analysis(a), QByteArray("analysis"));
        stream.reset();
        settle(*s, server.body.size());
        QCOMPARE(s->analysis(a), QByteArray("analysis"));
        // (Its download is let go a moment after its stream closes.)
        QTRY_COMPARE_WITH_TIMEOUT((s->setCapacity(0), s->usage()), 0, 5000);
        QVERIFY(s->analysis(a).isEmpty());

        s->setCapacity(64 << 20);
        download();
        if (QTest::currentTestFailed())
            return;
        s->keepAnalysis(a, "again");
        stream.reset();
        settle(*s, server.body.size());
        s->clear();
        QVERIFY(s->analysis(a).isEmpty());

        auto plain = a;
        plain.identity = ContentHash{}; // streams through a temporary file
        s->keepAnalysis(plain, "plain");
        QVERIFY(s->analysis(plain).isEmpty());
    }

    void partitionsAreSeparate()
    {
        test::RangeServer server{noise(Chunk * 2, 11)};
        auto s = store(server);
        {
            auto stream = s->open(source(server.body));
            auto read = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        }
        settle(*s, server.body.size());
        s->setPartition(QStringLiteral("account-b"));
        auto requests = server.ranges.size();
        {
            auto stream = s->open(source(server.body));
            auto read = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        }
        QVERIFY(server.ranges.size() > requests);
        settle(*s, server.body.size() * 2);
        s->setPartition(QStringLiteral("account-a"));
        requests = server.ranges.size();
        {
            auto stream = s->open(source(server.body));
            auto read = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        }
        QCOMPARE(server.ranges.size(), requests);
        s->clear(); // this partition only
        QTRY_COMPARE_WITH_TIMEOUT(s->usage(), server.body.size(), 5000);
        s->clear(true);
        QTRY_COMPARE_WITH_TIMEOUT(s->usage(), 0, 5000);
    }

    void cancelWakesABlockedRead()
    {
        test::RangeServer server{noise(Chunk * 8, 12)};
        server.intervalMs = 10000; // the first block, then a long wait
        server.blockBytes = 1024;
        auto s = store(server);
        auto stream = s->open(source(server.body));
        // The downloader may start directly at the seek position. Request more
        // than the first block so the read must still wait for further bytes.
        auto read = readAsync(stream, Chunk * 2, 4 * 1024);
        const auto cancelOnExit = qScopeGuard([stream] { stream->cancel(); });
        // Both a continuing head request and a request at the seek are valid.
        QTRY_VERIFY_WITH_TIMEOUT(!server.ranges.isEmpty(), 1000);
        QVERIFY(!ready(read));
        stream->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 1000);
        QCOMPARE(read.get(), QByteArray("FAILED"));
    }

    // Playing the same song again right after stopping it: the stopped handle's cancel
    // must not reach the new one (it shares the download until the old one is released).
    void cancelingOneHandleLeavesTheOthers()
    {
        test::RangeServer server{noise(Chunk * 2, 40)};
        server.intervalMs = 5;
        server.blockBytes = 16 * 1024;
        auto s = store(server);
        auto old = s->open(source(server.body));
        old->cancel();
        old->interrupt();
        auto again = s->open(source(server.body));
        old.reset();
        auto read = readAsync(again);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
    }

    void interruptWakesOneRead()
    {
        test::RangeServer server{noise(Chunk * 8, 16)};
        server.intervalMs = 10000; // the first block, then a long wait
        server.blockBytes = 1024;
        auto s = store(server);
        auto stream = s->open(source(server.body));
        // More than one block: wherever the download starts (the head, or at once from
        // here when the read is first), the second block is the one it waits for.
        auto blocked = readAsync(stream, Chunk * 4, 4 * 1024);
        QTest::qWait(100);
        stream->interrupt();
        QTRY_VERIFY_WITH_TIMEOUT(ready(blocked), 1000);
        QCOMPARE(blocked.get(), QByteArray("FAILED"));
        // Unlike cancel(), later reads still work.
        auto head = readAsync(stream, 0, 512);
        QTRY_VERIFY_WITH_TIMEOUT(ready(head), 1000);
        QCOMPARE(head.get(), server.body.left(512));
    }

    // A seek's read past the download that ends interrupted (the engine's wake-up for it)
    // still sends the download there: the seek tries again and finds it coming (2026-11-22).
    void anInterruptedReadStillMovesTheDownload()
    {
        test::RangeServer server{noise(Chunk * 8, 17)};
        server.intervalMs = 50;
        server.blockBytes = 16 * 1024;
        auto s = store(server);
        auto stream = s->open(source(server.body));
        stream->interrupt(); // no read waits: it stays for the next one that would
        auto read = readAsync(stream, Chunk * 6, 1024);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 1000);
        QCOMPARE(read.get(), QByteArray("FAILED"));
        const QByteArray there = "bytes=" + QByteArray::number(Chunk * 6) + '-';
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(server.ranges.cbegin(), server.ranges.cend(),
                                             [&](auto& range) { return range.startsWith(there); }),
                                 2000);
        auto again = readAsync(stream, Chunk * 6, 1024);
        QTRY_VERIFY_WITH_TIMEOUT(ready(again), 2000);
        QCOMPARE(again.get(), server.body.mid(Chunk * 6, 1024));
    }

    void prefetchedHeadIsReused()
    {
        test::RangeServer server{noise(Chunk * 8, 13)};
        auto s = store(server);
        s->prefetch(source(server.body), Chunk * 2);
        QTRY_COMPARE_WITH_TIMEOUT(s->usage(), Chunk * 2, 5000);
        server.ranges.clear();
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        QVERIFY(!server.ranges.isEmpty());
        QVERIFY(server.ranges.first().startsWith("bytes=" + QByteArray::number(Chunk * 2) + '-'));
    }

    void withoutMd5NothingIsKept()
    {
        test::RangeServer server{noise(Chunk * 2, 14)};
        auto s = store(server);
        auto stream = s->open(source(server.body, false));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        stream.reset();
        QTest::qWait(50);
        QCOMPARE(s->usage(), 0);
    }

    // A stream-addressed source (Bilibili) is kept too, keyed by its stream name, with no
    // digest: the finished file is trusted as it is.
    void streamAddressedSourceIsKept()
    {
        test::RangeServer server{noise(Chunk * 2, 31)};
        auto s = store(server);
        auto stream = s->open(biliSource(server.body));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        stream.reset();
        settle(*s, server.body.size());
        QCOMPARE(dataFiles(), 1);
        const auto requests = server.ranges.size();
        auto again = s->open(biliSource(server.body));
        auto second = readAsync(again);
        QTRY_VERIFY_WITH_TIMEOUT(ready(second), 5000);
        QCOMPARE(second.get(), server.body);
        QCOMPARE(server.ranges.size(), requests); // served from the cache, not downloaded again
    }

    // Bilibili's CDN needs its Referer and User-Agent, and still never a Cookie.
    void bilibiliRequestsCarryItsHeaders()
    {
        test::RangeServer server{noise(Chunk * 2, 32)};
        auto s = store(server);
        auto read = readAsync(s->open(biliSource(server.body)));
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        QVERIFY(!server.requests.isEmpty());
        const auto& request = server.requests.first();
        QCOMPARE(request.rawHeader("Referer"), QByteArray("https://www.bilibili.com/"));
        QCOMPARE(request.rawHeader("User-Agent"), QByteArray("Mozilla/5.0 CirrusTest"));
        QVERIFY(!request.hasRawHeader("Cookie"));
    }

    // The download rules (docs/tasks/audio-engine.md §5): no cookies or account headers,
    // redirects handled by us and kept on the CDN over HTTPS.
    void requestsCarryNoCredentials()
    {
        test::RangeServer server{noise(Chunk * 2, 21)};
        auto s = store(server);
        auto read = readAsync(s->open(source(server.body)));
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        QVERIFY(!server.requests.isEmpty());
        for (const auto& request : std::as_const(server.requests)) {
            QVERIFY(!request.hasRawHeader("Cookie"));
            QVERIFY(!request.hasRawHeader("Authorization"));
            QCOMPARE(request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),
                     int(QNetworkRequest::Manual));
            QCOMPARE(request.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt(),
                     int(QNetworkRequest::Manual));
            QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                     int(QNetworkRequest::ManualRedirectPolicy));
        }
    }

    void redirectsStayOnTheCdn()
    {
        test::RangeServer server{noise(Chunk * 2, 22)};
        server.statuses = {302};
        server.locations = {"https://m802.music.126.net/next.mp3"};
        auto s = store(server);
        auto read = readAsync(s->open(source(server.body)));
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), server.body);
        QCOMPARE(server.urls.value(1), QUrl(QStringLiteral("https://m802.music.126.net/next.mp3")));
        // A redirect to plain HTTP (a downgrade) or off the CDN is refused.
        for (const QByteArray location : {QByteArray("http://m802.music.126.net/next.mp3"),
                                          QByteArray("https://example.com/next.mp3")}) {
            test::RangeServer other{noise(Chunk, 23)};
            other.statuses = {302};
            other.locations = {location};
            auto refused = store(other);
            auto stream = refused->open(source(other.body, true, "b.mp3"));
            auto failed = readAsync(stream);
            QTRY_VERIFY_WITH_TIMEOUT(ready(failed), 5000);
            QCOMPARE(failed.get(), QByteArray("FAILED"));
            QCOMPARE(stream->failure(), std::optional(PlaybackError::UnsafeSource));
            QCOMPARE(other.urls.size(), 1);
        }
    }

    void redirectLoopIsBounded()
    {
        test::RangeServer server{noise(Chunk, 24)};
        server.statuses = {302, 302, 302, 302, 302};
        server.locations = {"/loop", "/loop", "/loop", "/loop", "/loop"};
        auto s = store(server);
        auto stream = s->open(source(server.body));
        auto read = readAsync(stream);
        QTRY_VERIFY_WITH_TIMEOUT(ready(read), 5000);
        QCOMPARE(read.get(), QByteArray("FAILED"));
        QCOMPARE(stream->failure(), std::optional(PlaybackError::UnsafeSource));
        QCOMPARE(server.urls.size(), 4); // the first request and three redirects
    }

    void refusesUnsafeOrHugeSources()
    {
        test::RangeServer server{noise(1024, 15)};
        auto s = store(server);
        auto unsafe = source(server.body);
        unsafe.url = QUrl(QStringLiteral("https://example.com/a.mp3"));
        PlaybackError error{};
        QVERIFY(!s->open(unsafe, &error));
        QCOMPARE(error, PlaybackError::UnsafeSource);
        auto huge = source(server.body);
        huge.size = 1025LL * 1024 * 1024; // past 1 GiB
        QVERIFY(!s->open(huge, &error));
        QCOMPARE(error, PlaybackError::TooLarge);
    }
};
QTEST_GUILESS_MAIN(AudioStoreTest)
#include "audio_store_test.moc"
