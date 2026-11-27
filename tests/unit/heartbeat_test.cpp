#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
QJsonObject json(QByteArray value) { return QJsonDocument::fromJson(value).object(); }
MusicResult result(const QSignalSpy& spy) { return qvariant_cast<MusicResult>(spy.last().at(2)); }
HeartbeatQuery query(bool fromAll = false)
{
    return {PlaylistId{"70"}, SongId{"2"}, SongId{"1"}, 3, fromAll};
}
QJsonObject parameters()
{
    return {{"playlistId", "70"},
            {"songId", "2"},
            {"startMusicId", "1"},
            {"count", "3"},
            {"type", "fromPlayOne"}};
}
MusicResult map(QByteArray body) { return mapMusicResponse(MusicQuery{query()}, json(body)); }
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    MusicGateway gateway{client};
    MusicQueryService service{session, gateway};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& http)
    {
        auto m = std::make_unique<FakeManager>();
        http = m.get();
        m->plans = std::move(plans);
        return std::make_unique<HttpTransport>(std::move(m));
    }
    explicit Harness(QList<Plan> plans, bool authenticated = true)
        : client(transport(std::move(plans), http), "synthetic-heartbeat")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (authenticated) {
            const auto attempt = session.beginAuthentication();
            const auto prepared = session.prepareAuthentication(attempt, "1");
            QVERIFY(prepared);
            QVERIFY(session.completeAuthentication(*prepared));
            // Synthetic credential fixture only.
            Plan seed{R"({"code":200,"unikey":"synthetic"})"};
            seed.headers.append(std::pair<QByteArray, QByteArray>{
                "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
            http->plans.prepend(seed);
            QSignalSpy done(&client, &SessionApiClient::completed);
            QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
            QTRY_COMPARE(done.size(), 1);
        }
    }
};
} // namespace
class HeartbeatTest : public QObject
{
    Q_OBJECT
  private slots:
    void contract()
    {
        const auto c = operationContract(ApiOperation::HeartbeatList);
        QVERIFY(c);
        QCOMPARE(c->logicalPath, "/api/playmode/intelligence/list");
        QCOMPARE(c->protocol, Protocol::Eapi);
        QVERIFY(requiresAccountCookie(ApiOperation::HeartbeatList));
        QVERIFY(!retriesLostConnection(ApiOperation::HeartbeatList)); // a new mix each time
        DecodedResponse r;
        r.businessCode = 200;
        QVERIFY(!acceptsResponseCookies(ApiOperation::HeartbeatList, r));
        QVERIFY(requiresAuthenticatedMusicQuery(query()));
    }
    void queryLimits()
    {
        QVERIFY(query().isValid());
        auto q = query();
        q.count = 0;
        QVERIFY(!q.isValid());
        q.count = HeartbeatQuery::MaximumCount;
        QVERIFY(q.isValid());
        q.count = HeartbeatQuery::MaximumCount + 1;
        QVERIFY(!q.isValid());
        q = query();
        q.playlist = PlaylistId{"0"};
        QVERIFY(!q.isValid());
        q = query();
        q.anchor = SongId{"01"};
        QVERIFY(!q.isValid());
        q = query();
        q.start = SongId{};
        QVERIFY(!q.isValid());
    }
    void payload()
    {
        QCOMPARE(*operationPayload(ApiOperation::HeartbeatList, parameters()),
                 json(R"({"songId":"2","type":"fromPlayOne","playlistId":"70",)"
                      R"("startMusicId":"1","count":"3","extJson":"{}"})"));
        auto all = parameters();
        all["type"] = "fromPlayAll";
        QCOMPARE(operationPayload(ApiOperation::HeartbeatList, all)->value("type"),
                 QJsonValue("fromPlayAll"));
        const QList<std::pair<QString, QJsonValue>> rejected{
            {"type", "fromPlayList"}, {"type", 1},      {"count", "0"},
            {"count", "10001"},       {"count", 3},     {"count", "three"},
            {"playlistId", "0"},      {"songId", "01"}, {"startMusicId", ""},
            {"playlistId", 70},       {"songId", "-2"},
        };
        for (const auto& [key, value] : rejected) {
            auto p = parameters();
            p[key] = value;
            QVERIFY2(!operationPayload(ApiOperation::HeartbeatList, p), qPrintable(key));
        }
        auto extra = parameters();
        extra["extJson"] = "{}";
        QVERIFY(!operationPayload(ApiOperation::HeartbeatList, extra));
        auto missing = parameters();
        missing.remove("startMusicId");
        QVERIFY(!operationPayload(ApiOperation::HeartbeatList, missing));
    }
    void mapping()
    {
        const auto list = std::get<HeartbeatList>(map(
            R"({"code":200,"data":[)"
            R"({"id":1,"songInfo":{"id":1,"name":"own","dt":1000},"alg":null,"recommended":false},)"
            R"({"id":"9007199254740993","songInfo":{"id":"9007199254740993","name":"mix",)"
            R"("ar":[{"id":4}],"al":{"id":5},"dt":2000},"alg":"alg-a","recommended":true},)"
            R"({"id":2,"recommended":false},)"
            R"({"id":3,"songInfo":null})"
            R"(]})"));
        QCOMPARE(list.entries.size(), 4);
        QCOMPARE(list.entries[0].song.value, "1");
        QVERIFY(list.entries[0].details);
        QCOMPARE(list.entries[0].details->durationMs, std::optional<qint64>(1000));
        QVERIFY(list.entries[0].alg.isEmpty());
        QVERIFY(!list.entries[0].recommended);
        const auto& mix = list.entries[1];
        QCOMPARE(mix.song.value, "9007199254740993");
        QVERIFY(mix.recommended);
        QCOMPARE(mix.alg, "alg-a");
        QCOMPARE(mix.details->album->id->value, "5");
        QVERIFY(!list.entries[2].details); // the playlist's own: the caller has them
        QVERIFY(!list.entries[3].details);
        QVERIFY(!list.entries[3].recommended);
        QVERIFY(std::get<HeartbeatList>(map(R"({"code":200,"data":[]})")).entries.isEmpty());
    }
    void recommendedWithoutDetailsLeftOut()
    {
        const auto list = std::get<HeartbeatList>(
            map(R"({"code":200,"data":[{"id":1},{"id":6,"recommended":true},)"
                R"({"id":7,"songInfo":{"id":8},"recommended":true},)"
                R"({"id":9,"songInfo":{"id":"x"},"recommended":true},{"id":2}]})"));
        QCOMPARE(list.entries.size(), 2);
        QCOMPARE(list.entries[0].song.value, "1");
        QCOMPARE(list.entries[1].song.value, "2");
        // The playlist's own song with mismatched details keeps its place without them.
        const auto own =
            std::get<HeartbeatList>(map(R"({"code":200,"data":[{"id":1,"songInfo":{"id":2}}]})"));
        QCOMPARE(own.entries.size(), 1);
        QVERIFY(!own.entries[0].details);
    }
    void duplicatesKeepFirstPlace()
    {
        const auto list = std::get<HeartbeatList>(
            map(R"({"code":200,"data":[{"id":1},{"id":2},)"
                R"({"id":1,"songInfo":{"id":1},"alg":"later","recommended":true}]})"));
        QCOMPARE(list.entries.size(), 2);
        QCOMPARE(list.entries[0].song.value, "1");
        QCOMPARE(list.entries[0].alg, "later");
        QVERIFY(list.entries[0].recommended);
        QCOMPARE(list.entries[1].song.value, "2");
    }
    void malformed_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("code") << QByteArray(R"({"code":301,"data":[]})");
        QTest::newRow("missing data") << QByteArray(R"({"code":200})");
        QTest::newRow("object data") << QByteArray(R"({"code":200,"data":{}})");
        QTest::newRow("row not object") << QByteArray(R"({"code":200,"data":[1]})");
        QTest::newRow("missing id") << QByteArray(R"({"code":200,"data":[{"recommended":true}]})");
        QTest::newRow("bad id") << QByteArray(R"({"code":200,"data":[{"id":"01"}]})");
        QTest::newRow("unsafe numeric id")
            << QByteArray(R"({"code":200,"data":[{"id":9007199254740993}]})");
        QTest::newRow("songInfo array")
            << QByteArray(R"({"code":200,"data":[{"id":1,"songInfo":[]}]})");
        QTest::newRow("alg number") << QByteArray(R"({"code":200,"data":[{"id":1,"alg":3}]})");
        QTest::newRow("recommended text")
            << QByteArray(R"({"code":200,"data":[{"id":1,"recommended":"true"}]})");
    }
    void malformed()
    {
        QFETCH(QByteArray, body);
        QCOMPARE(std::get<MusicFailure>(map(body)).error, MusicError::InvalidResponse);
    }
    void sizeLimit()
    {
        QJsonArray rows;
        for (int i = 0; i < 2 * HeartbeatQuery::MaximumCount + 1; ++i)
            rows.append(QJsonObject{{"id", i + 1}});
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(query(), {{"code", 200}, {"data", rows}})));
        rows.removeLast();
        QCOMPARE(std::get<HeartbeatList>(mapMusicResponse(query(), {{"code", 200}, {"data", rows}}))
                     .entries.size(),
                 2 * HeartbeatQuery::MaximumCount);
    }
    void guestRejectedBeforeNetwork()
    {
        Harness h({}, false);
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token, query()));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::AuthenticationRequired);
        QCOMPARE(h.http->requests.size(), 0);
    }
    void invalidQueryNotSent()
    {
        Harness h({});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        auto q = query();
        q.count = 0;
        QVERIFY(h.service.request(h.session.snapshot().token, q));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::InvalidRequest);
        QCOMPARE(h.http->requests.size(), 1); // the sign-in fixture only
    }
    void nativeRequest()
    {
        Plan reply{R"({"code":200,"data":[{"id":1},{"id":4,"songInfo":{"id":4},)"
                   R"("alg":"alg-b","recommended":true}]})"};
        reply.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-untrusted; Domain=.music.163.com; Path=/"});
        Harness h({reply, Plan{R"({"code":200,"data":[]})"}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        QVERIFY(h.service.request(token, query(true)));
        QTRY_COMPARE(done.size(), 1);
        const auto list = std::get<HeartbeatList>(result(done));
        QCOMPARE(list.entries.size(), 2);
        QCOMPARE(list.entries[1].alg, "alg-b");
        QCOMPARE(h.http->requests.last().url().path(), "/eapi/playmode/intelligence/list");
        QVERIFY(h.http->requests.last().rawHeader("Cookie").contains("MUSIC_U=synthetic-original"));
        const auto wire = decodeEapiRequest(h.http->bodies.last());
        QVERIFY(wire);
        QCOMPARE(wire->logicalPath, "/api/playmode/intelligence/list");
        auto payload = wire->payload;
        QVERIFY(payload.take("header").isObject());
        payload.remove("e_r");
        QCOMPARE(payload, json(R"({"songId":"2","type":"fromPlayAll","playlistId":"70",)"
                               R"("startMusicId":"1","count":"3","extJson":"{}"})"));
        // The reply's cookie is not taken.
        QVERIFY(h.service.request(token, query()));
        QTRY_COMPARE(done.size(), 2);
        QVERIFY(!h.http->requests.last().rawHeader("Cookie").contains("synthetic-untrusted"));
    }
};
QTEST_GUILESS_MAIN(HeartbeatTest)
#include "heartbeat_test.moc"
