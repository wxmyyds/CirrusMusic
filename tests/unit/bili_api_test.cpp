#include "../support/fake_http.h"
#include "../support/synthetic_bilibili.h"
#include "infrastructure/bilibili/bili_api.h"
#include <QUrlQuery>

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace cirrus;
using namespace cirrus::bilibili;

namespace
{
SongTargetInfo song()
{
    return SongTargetInfo{QStringLiteral("Song"), QStringLiteral("Artist"), 200000};
}
} // namespace

class BiliApiTest : public QObject
{
    Q_OBJECT
  private slots:
    void successWhenPlayUrlCarriesTheLength()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        BiliApi api(std::move(manager));
        fake->plans = test::biliResolveSized();
        QSignalSpy spy(&api, &BiliApi::resolved);
        QVERIFY(api.resolve(song(), Quality::High));
        QTRY_COMPARE(spy.count(), 1);
        const auto result = qvariant_cast<BiliApi::Result>(spy.at(0).at(1));
        QVERIFY(result.stream.has_value());
        QCOMPARE(result.stream->size, qint64(4800000));
        QCOMPARE(result.bvid, QStringLiteral("BV1testpage"));
        QCOMPARE(result.cid, QStringLiteral("456"));
        QCOMPARE(result.failure, QString());
        QVERIFY(!result.needPermission);
        QCOMPARE(fake->requests.size(), 5);
        const QUrlQuery query(fake->requests.last().url());
        QCOMPARE(query.queryItemValue(QStringLiteral("fnval")), QStringLiteral("16"));
        QCOMPARE(query.queryItemValue(QStringLiteral("platform")), QStringLiteral("pc"));
        QCOMPARE(query.queryItemValue(QStringLiteral("fnver")), QStringLiteral("0"));
        QCOMPARE(query.queryItemValue(QStringLiteral("fourk")), QStringLiteral("0"));
        QVERIFY(!query.hasQueryItem(QStringLiteral("qn")));
    }

    void successViaSizeProbeWhenLengthMissing()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        BiliApi api(std::move(manager));
        test::Plan probe;
        probe.body = QByteArrayLiteral("\0");
        probe.status = 206;
        probe.headers = {{"Content-Range", "bytes 0-0/4800000"}};
        fake->plans = {test::biliJson(test::kBiliFingerprint),    test::biliJson(test::kBiliNav),
                       test::biliJson(test::kBiliSearch),         test::biliJson(test::kBiliView),
                       test::biliJson(test::kBiliPlayUrlUnsized), probe};
        QSignalSpy spy(&api, &BiliApi::resolved);
        QVERIFY(api.resolve(song(), Quality::High));
        QTRY_COMPARE(spy.count(), 1);
        const auto result = qvariant_cast<BiliApi::Result>(spy.at(0).at(1));
        QVERIFY(result.stream.has_value());
        QCOMPARE(result.stream->size, qint64(4800000));
        QCOMPARE(fake->requests.size(), 6);
        // The probe is the only GET without a Cookie; a CDN fetch must never see one.
        const auto& probeRequest = fake->requests.last();
        QVERIFY(probeRequest.hasRawHeader("Range"));
        QVERIFY(!probeRequest.hasRawHeader("Cookie"));
        QVERIFY(fake->requests.at(2).hasRawHeader("Cookie")); // the search call does
    }

    void withheldAtPlayUrlIsNeedPermission()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        BiliApi api(std::move(manager));
        fake->plans = {test::biliJson(test::kBiliFingerprint), test::biliJson(test::kBiliNav),
                       test::biliJson(test::kBiliSearch), test::biliJson(test::kBiliView),
                       test::biliJson(R"({"code":-404,"message":"not found"})")};
        QSignalSpy spy(&api, &BiliApi::resolved);
        QVERIFY(api.resolve(song(), Quality::High));
        QTRY_COMPARE(spy.count(), 1);
        const auto result = qvariant_cast<BiliApi::Result>(spy.at(0).at(1));
        QVERIFY(!result.stream.has_value());
        QVERIFY(result.needPermission);
        QCOMPARE(result.failure, QStringLiteral("code:-404"));
    }

    void riskControlIsFinalNotRetried()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        BiliApi api(std::move(manager));
        test::Plan wall;
        wall.status = 412;
        wall.body = "blocked";
        fake->plans = {test::biliJson(test::kBiliFingerprint), test::biliJson(test::kBiliNav),
                       wall};
        QSignalSpy spy(&api, &BiliApi::resolved);
        QVERIFY(api.resolve(song(), Quality::High));
        QTRY_COMPARE(spy.count(), 1);
        const auto result = qvariant_cast<BiliApi::Result>(spy.at(0).at(1));
        QVERIFY(!result.stream.has_value());
        QCOMPARE(result.failure, QStringLiteral("risk-control"));
        // Fingerprint + nav + the one blocked search: no retry.
        QCOMPARE(fake->requests.size(), 3);
    }

    void anonymousNavStillProvidesWbiKey()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        BiliApi api(std::move(manager));
        auto nav = test::kBiliNav;
        nav.replace("\"code\":0", "\"code\":-101");
        fake->plans = test::biliResolveSized();
        fake->plans[1] = test::biliJson(nav);
        QSignalSpy spy(&api, &BiliApi::resolved);
        api.resolve(song(), Quality::High);
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(qvariant_cast<BiliApi::Result>(spy.first().at(1)).stream);
    }

    void fingerprintRiskControlIsFinal()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        BiliApi api(std::move(manager));
        test::Plan wall;
        wall.status = 412;
        wall.error = QNetworkReply::UnknownContentError;
        fake->plans = {wall};
        QSignalSpy spy(&api, &BiliApi::resolved);
        api.resolve(song(), Quality::High);
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(qvariant_cast<BiliApi::Result>(spy.first().at(1)).failure,
                 QStringLiteral("risk-control"));
        QCOMPARE(fake->requests.size(), 1);
    }

    void concurrentRequestsKeepTheirOwnIds()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        BiliApi api(std::move(manager));
        for (const auto& plan : test::biliResolveSized()) {
            fake->plans.append(plan);
            fake->plans.append(plan);
        }
        QSignalSpy spy(&api, &BiliApi::resolved);
        const auto first = api.resolve(song(), Quality::High);
        const auto second = api.resolve(song(), Quality::High);
        QVERIFY(first != second);
        QCOMPARE(spy.count(), 0);
        QTRY_COMPARE(spy.count(), 2);
        QCOMPARE(spy[0][0].toULongLong(), first);
        QCOMPARE(spy[1][0].toULongLong(), second);
        QVERIFY(qvariant_cast<BiliApi::Result>(spy[0][1]).stream);
        QVERIFY(qvariant_cast<BiliApi::Result>(spy[1][1]).stream);
        QCOMPARE(fake->requests.size(), 10);
    }

    void cancelBeforeDispatchProducesNoRequest()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        BiliApi api(std::move(manager));
        QSignalSpy spy(&api, &BiliApi::resolved);
        api.cancel(api.resolve(song(), Quality::High));
        QCoreApplication::processEvents();
        QCOMPARE(fake->requests.size(), 0);
        QCOMPARE(spy.count(), 0);
    }

    void noMatchWhenNothingScores()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        BiliApi api(std::move(manager));
        const QByteArray other = R"({"code":0,"data":{"result":[
            {"type":"video","aid":9,"bvid":"BVother","title":"Cooking Show",
             "author":"Chef","duration":"10:00","play":5}]}})";
        const QByteArray viewOther = R"({"code":0,"data":{"aid":9,"bvid":"BVother",
            "title":"Cooking Show","owner":{"name":"Chef"},"duration":600,
            "pages":[{"cid":789,"page":1,"part":"Episode 1","duration":600}]}})";
        const QByteArray empty = R"({"code":0,"data":{"result":[]}})";
        fake->plans = {test::biliJson(test::kBiliFingerprint),
                       test::biliJson(test::kBiliNav),
                       test::biliJson(other),
                       test::biliJson(viewOther),
                       test::biliJson(empty),
                       test::biliJson(empty),
                       test::biliJson(empty)};
        QSignalSpy spy(&api, &BiliApi::resolved);
        QVERIFY(api.resolve(song(), Quality::High));
        QTRY_COMPARE(spy.count(), 1);
        const auto result = qvariant_cast<BiliApi::Result>(spy.at(0).at(1));
        QVERIFY(!result.stream.has_value());
        QCOMPARE(result.failure, QStringLiteral("no-match"));
        QVERIFY(!result.needPermission);
    }
};

QTEST_GUILESS_MAIN(BiliApiTest)
#include "bili_api_test.moc"
