#include "infrastructure/bilibili/bili_http_client.h"
#include "../support/synthetic_bilibili.h"

#include <QSignalSpy>
#include <QTest>

using namespace cirrus;
using namespace cirrus::bilibili;

class BiliHttpTest : public QObject
{
    Q_OBJECT
  private slots:
    void cookieCannotReachCdn()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        HttpClient client(std::move(manager));
        QSignalSpy spy(&client, &HttpClient::completed);
        QVERIFY(client.get(QUrl("https://cn.bilivideo.com/a"), "SESSDATA=synthetic", true));
        QVERIFY(!fake->requests.first().hasRawHeader("Cookie"));
        QCOMPARE(fake->requests.first().attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),
                 int(QNetworkRequest::Manual));
        QTRY_COMPARE(spy.count(), 1);
    }

    void subtitleScopeCannotSendCookieOrFetchAudio()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        HttpClient client(std::move(manager));
        QVERIFY(client.get(QUrl("https://aisubtitle.hdslb.com/a"), "SESSDATA=synthetic", true,
                           15000, 4096, {}, HttpScope::Subtitle));
        QVERIFY(!fake->requests.first().hasRawHeader("Cookie"));
        QCOMPARE(client.get(QUrl("https://cn.bilivideo.com/a"), {}, false, 15000, 4096, {},
                            HttpScope::Subtitle), quint64(0));
        QCOMPARE(client.get(QUrl("https://api.bilibili.com/a"), {}, false, 15000, 4096, {},
                            HttpScope::Subtitle), quint64(0));
        QCOMPARE(client.get(QUrl("https://aisubtitle.hdslb.com/a"), {}, false), quint64(0));
        QCOMPARE(fake->requests.size(), 1);
    }

    void unsafeUrlsNeverDispatch()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        HttpClient client(std::move(manager));
        for (const auto* raw : {"https://evil.example/a", "file:///tmp/a",
                                "https://user@api.bilibili.com/a", "http://api.bilibili.com/a",
                                "https://evilbilivideo.com/a", "https://cn.bilivideo.com/a#frag"})
            QCOMPARE(client.get(QUrl(QString::fromLatin1(raw)), "secret", true), quint64(0));
        QCOMPARE(fake->requests.size(), 0);
    }

    void timeoutHasItsOwnFailure()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        test::Plan hanging;
        hanging.delayMs = -1;
        fake->plans = {hanging};
        HttpClient client(std::move(manager));
        QSignalSpy spy(&client, &HttpClient::completed);
        client.get(QUrl("https://api.bilibili.com/a"), {}, false, 5);
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(qvariant_cast<HttpOutcome>(spy.first().at(1)).error, QStringLiteral("timeout"));
    }

    void sizeHeaders_data()
    {
        QTest::addColumn<QByteArray>("contentRange");
        QTest::addColumn<QByteArray>("encoding");
        QTest::addColumn<qint64>("expected");
        QTest::newRow("valid") << QByteArray("bytes 0-0/4800000") << QByteArray() << qint64(4800000);
        QTest::newRow("malformed") << QByteArray("anything/4800000") << QByteArray() << qint64(-1);
        QTest::newRow("inverted") << QByteArray("bytes 2-1/4800000") << QByteArray() << qint64(-1);
        QTest::newRow("outside") << QByteArray("bytes 0-5/5") << QByteArray() << qint64(-1);
        QTest::newRow("encoded") << QByteArray("bytes 0-0/4800000") << QByteArray("gzip") << qint64(-1);
    }

    void sizeHeaders()
    {
        QFETCH(QByteArray, contentRange);
        QFETCH(QByteArray, encoding);
        QFETCH(qint64, expected);
        auto manager = std::make_unique<test::FakeGetManager>();
        test::Plan plan;
        plan.status = 206;
        plan.body = "x";
        plan.headers = {{"Content-Range", contentRange}, {"Content-Encoding", encoding}};
        manager->plans = {plan};
        HttpClient client(std::move(manager));
        QSignalSpy spy(&client, &HttpClient::completed);
        client.get(QUrl("https://cn.bilivideo.com/a"), {}, false, 1000, 4096, "bytes=0-0");
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(qvariant_cast<HttpOutcome>(spy.first().at(1)).resourceBytes, expected);
    }

    void cancelSuppressesCompletion()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        test::Plan hanging;
        hanging.delayMs = -1;
        manager->plans = {hanging};
        HttpClient client(std::move(manager));
        QSignalSpy spy(&client, &HttpClient::completed);
        client.cancel(client.get(QUrl("https://api.bilibili.com/a"), {}, false));
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(BiliHttpTest)
#include "bili_http_test.moc"
