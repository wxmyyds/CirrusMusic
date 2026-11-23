#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "infrastructure/netease/api/collection_operation.h"
#include "infrastructure/netease/music/collection_gateway.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>
#include <type_traits>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
constexpr auto wide = "9007199254740993";
CollectionMutation mutation(int mode = 0)
{
    const auto action = mode % 2 ? CollectionAction::Remove : CollectionAction::Collect;
    CollectionChange c;
    switch (mode / 2) {
    case 0:
        c = SongHeart{{wide}, mode == 0};
        break;
    case 1:
        c = PlaylistCollection{{"2"}, {{wide}, {"4"}}, action};
        break;
    case 2:
        c = AlbumCollection{{wide}, action};
        break;
    case 3:
        c = ArtistCollection{{wide}, action};
        break;
    default:
        c = PodcastSubscription{{wide}, action};
        break;
    }
    return {QUuid::createUuid(), c};
}
const char* names[] = {"heart",    "unheart", "collect-songs", "remove-songs", "album",
                       "un-album", "artist",  "un-artist",     "podcast",      "un-podcast"};
const char* paths[] = {"/api/radio/like",
                       "/api/radio/like",
                       "/api/playlist/manipulate/tracks",
                       "/api/playlist/manipulate/tracks",
                       "/api/album/sub",
                       "/api/album/unsub",
                       "/api/artist/sub",
                       "/api/artist/unsub",
                       "/api/djradio/sub",
                       "/api/djradio/unsub"};
ApiOperation operations[] = {ApiOperation::SongHeart,
                             ApiOperation::SongHeart,
                             ApiOperation::PlaylistCollectSongs,
                             ApiOperation::PlaylistCollectSongs,
                             ApiOperation::AlbumSubscribe,
                             ApiOperation::AlbumUnsubscribe,
                             ApiOperation::ArtistSubscribe,
                             ApiOperation::ArtistUnsubscribe,
                             ApiOperation::PodcastSubscribe,
                             ApiOperation::PodcastUnsubscribe};
CollectionResult result(const QSignalSpy& done)
{
    return qvariant_cast<CollectionResult>(done.last()[2]);
}
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    CollectionGateway gateway{session, client};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& http)
    {
        auto manager = std::make_unique<FakeManager>();
        http = manager.get();
        http->plans = plans;
        return std::make_unique<HttpTransport>(std::move(manager));
    }
    Harness(QList<Plan> plans, bool auth = true, bool cookie = true)
        : client(transport(plans, http), "synthetic-collection")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto attempt = session.beginAuthentication();
        const auto prepared = session.prepareAuthentication(attempt, "101");
        QVERIFY(prepared);
        QVERIFY(session.completeAuthentication(*prepared));
        if (!cookie)
            return;
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans.prepend(seed);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
    quint64 submit(CollectionMutation m = mutation(), QString account = "101")
    {
        return gateway.submit(session.snapshot().token, account, m);
    }
};
void ready(BackendFacade& f, SessionPhase phase = SessionPhase::Authenticated)
{
    f.start();
    BackendEvent started;
    started.kind = BackendEvent::Kind::Started;
    f.deliver(started);
    BackendEvent session;
    session.kind = BackendEvent::Kind::Session;
    session.session = {{QUuid::createUuid(), 1}, phase, "101"};
    f.deliver(session);
}
} // namespace
class CollectionsTest : public QObject
{
    Q_OBJECT
  private slots:
    void validation()
    {
        for (int i = 0; i < 10; ++i)
            QVERIFY(mutation(i).isValid());
        auto m = mutation();
        m.eventId = {};
        QVERIFY(!m.isValid());
        m = mutation();
        m.change = SongHeart{{"0"}, false};
        QVERIFY(!m.isValid());
        for (const QString id : {"", "01", "-1", "1.0", " 1", "9223372036854775808"}) {
            m = mutation();
            m.change = SongHeart{{id}, false};
            QVERIFY(!m.isValid());
            m.change = AlbumCollection{{id}};
            QVERIFY(!m.isValid());
            m.change = ArtistCollection{{id}};
            QVERIFY(!m.isValid());
            m.change = PodcastSubscription{{id}};
            QVERIFY(!m.isValid());
        }
        for (const auto action : {CollectionAction(-1), CollectionAction(2)}) {
            m.change = AlbumCollection{{"1"}, action};
            QVERIFY(!m.isValid());
            m.change = ArtistCollection{{"1"}, action};
            QVERIFY(!m.isValid());
            m.change = PodcastSubscription{{"1"}, action};
            QVERIFY(!m.isValid());
            m.change = PlaylistCollection{{"2"}, {{"1"}}, action};
            QVERIFY(!m.isValid());
        }
        PlaylistCollection c{{"2"}, {}};
        m = {QUuid::createUuid(), c};
        QVERIFY(!m.isValid());
        c.songs = {{"1"}, {"1"}};
        m.change = c;
        QVERIFY(!m.isValid());
        c.songs.clear();
        for (int i = 1; i <= 100; ++i)
            c.songs.append({QString::number(i)});
        m.change = c;
        QVERIFY(m.isValid());
        QVERIFY(collectionRequest(m));
        c.songs.append({"101"});
        m.change = c;
        QVERIFY(!m.isValid());
        static_assert(!std::is_convertible_v<PodcastId, SongId>);
        static_assert(!std::is_convertible_v<PlaylistId, AlbumId>);
    }
    void contracts_data()
    {
        QTest::addColumn<int>("mode");
        for (int i = 0; i < 10; ++i)
            QTest::newRow(names[i]) << i;
    }
    void contracts()
    {
        QFETCH(int, mode);
        const auto request = collectionRequest(mutation(mode));
        QVERIFY(request);
        QCOMPARE(request->operation, operations[mode]);
        QVERIFY(isCollectionOperation(request->operation));
        const auto c = operationContract(request->operation);
        QVERIFY(c);
        QCOMPARE(c->logicalPath, paths[mode]);
        QCOMPARE(c->responseLimit, 64 * 1024);
        QCOMPARE(c->protocol, mode / 2 == 1 ? Protocol::Eapi : Protocol::Weapi);
        QCOMPARE(c->destination, OfficialDestination::Default);
        QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
        QVERIFY(requiresAccountCookie(request->operation));
        DecodedResponse accepted;
        accepted.businessCode = 200;
        QVERIFY(!acceptsResponseCookies(request->operation, accepted));
        const auto p = operationPayload(request->operation, request->parameters);
        QVERIFY(p);
        if (mode / 2 == 0) {
            QCOMPARE(
                *p,
                (QJsonObject{
                    {"alg", "itembased"}, {"trackId", wide}, {"like", mode == 0}, {"time", "3"}}));
        } else if (mode / 2 == 1) {
            QCOMPARE(*p, (QJsonObject{{"pid", "2"},
                                      {"op", mode % 2 ? "del" : "add"},
                                      {"trackIds", "[\"9007199254740993\",\"4\"]"},
                                      {"imme", "true"}}));
        } else if (mode / 2 == 3) {
            QCOMPARE(*p, (QJsonObject{{"artistId", wide}, {"artistIds", "[9007199254740993]"}}));
        } else
            QCOMPARE(*p, (QJsonObject{{"id", wide}}));
        for (const char* key : {"url", "uid", "crypto", "e_r", "checkToken", "timestamp", "total",
                                "csrf_token", "header", "imme", "alg", "time"}) {
            auto bad = request->parameters;
            bad.insert(key, "overwrite");
            QVERIFY(!operationPayload(request->operation, bad));
        }
        auto bad = request->parameters;
        bad.insert(mode / 2 == 1 ? "pid" : "id", 1);
        QVERIFY(!operationPayload(request->operation, bad));
    }
    // As the official Android app 9.6.22 (docs/api/playlist-subscription.md): subscribe
    // with its Yidun checkToken, "" without one (what Cirrus sends); unsubscribe with the
    // id alone.
    void playlistSubscription()
    {
        for (const QString id : {"", "01", "-1", " 1"})
            QVERIFY(
                !CollectionMutation({QUuid::createUuid(), PlaylistSubscription{{id}}}).isValid());
        QVERIFY(!CollectionMutation(
                     {QUuid::createUuid(), PlaylistSubscription{{"1"}, CollectionAction(2)}})
                     .isValid());
        for (const bool subscribe : {true, false}) {
            const CollectionMutation m{QUuid::createUuid(),
                                       PlaylistSubscription{{wide},
                                                            subscribe ? CollectionAction::Collect
                                                                      : CollectionAction::Remove}};
            const auto request = collectionRequest(m);
            QVERIFY(request);
            QCOMPARE(request->operation, subscribe ? ApiOperation::PlaylistSubscribe
                                                   : ApiOperation::PlaylistUnsubscribe);
            const auto c = operationContract(request->operation);
            QVERIFY(c);
            QCOMPARE(c->logicalPath,
                     subscribe ? "/api/playlist/subscribe" : "/api/playlist/unsubscribe");
            QCOMPARE(c->protocol, Protocol::Eapi);
            QCOMPARE(c->destination, OfficialDestination::Default);
            QVERIFY(requiresAccountCookie(request->operation));
            const QJsonObject expected = subscribe ? QJsonObject{{"id", wide}, {"checkToken", ""}}
                                                   : QJsonObject{{"id", wide}};
            QCOMPARE(*operationPayload(request->operation, request->parameters), expected);
            // The caller never supplies the token or anything else.
            for (const char* key : {"checkToken", "t", "header", "e_r"}) {
                auto bad = request->parameters;
                bad.insert(key, "x");
                QVERIFY(!operationPayload(request->operation, bad));
            }
            Harness h({Plan{R"({"code":200})"}});
            QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
            QVERIFY(h.submit(m));
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(result(done).outcome, CollectionOutcome::Acknowledged);
            const auto& r = h.http->requests.last();
            QCOMPARE(r.url().path(), QString(subscribe ? "/eapi/playlist/subscribe"
                                                       : "/eapi/playlist/unsubscribe"));
            const auto wire = decodeEapiRequest(h.http->bodies.last());
            QVERIFY(wire);
            auto payload = wire->payload;
            QVERIFY(payload.take("header").isObject());
            QCOMPARE(payload.take("e_r"), QJsonValue(false));
            QCOMPARE(payload, expected);
        }
    }
    void wireBoundaries()
    {
        QVERIFY(!operationPayload(ApiOperation::SongHeart, {{"id", "1"}, {"liked", "false"}}));
        QVERIFY(!operationPayload(ApiOperation::SongHeart, {{"id", "1"}, {"liked", 0}}));
        for (QJsonValue v :
             {QJsonValue(QJsonArray{}), QJsonValue(QJsonArray{"1", "1"}), QJsonValue(QJsonArray{1}),
              QJsonValue("1,2"), QJsonValue(QJsonArray{"0"})})
            QVERIFY(!operationPayload(ApiOperation::PlaylistCollectSongs,
                                      {{"pid", "2"}, {"op", "add"}, {"ids", v}}));
        QVERIFY(!operationPayload(ApiOperation::PlaylistCollectSongs,
                                  {{"pid", "2"}, {"op", "unknown"}, {"ids", QJsonArray{"1"}}}));
        QJsonArray ids;
        for (int i = 1; i <= 100; ++i)
            ids.append(QString::number(i));
        QVERIFY(operationPayload(ApiOperation::PlaylistCollectSongs,
                                 {{"pid", "2"}, {"op", "del"}, {"ids", ids}}));
        ids.append("101");
        QVERIFY(!operationPayload(ApiOperation::PlaylistCollectSongs,
                                  {{"pid", "2"}, {"op", "del"}, {"ids", ids}}));
    }
    void native_data() { contracts_data(); }
    void native()
    {
        QFETCH(int, mode);
        Plan p{R"({"code":200})"};
        p.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({p, Plan{R"({"code":200})"}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto token = h.session.snapshot().token;
        for (int i = 0; i < 2; ++i) {
            const auto m = mutation(mode);
            QVERIFY(h.submit(m));
            QTRY_COMPARE(done.size(), i + 1);
            QCOMPARE(result(done).eventId, m.eventId);
            QCOMPARE(result(done).outcome, CollectionOutcome::Acknowledged);
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
            const auto& r = h.http->requests.last();
            QCOMPARE(r.url().path(),
                     QString(mode / 2 == 1 ? "/eapi" : "/weapi") + QString(paths[mode]).mid(4));
            QCOMPARE(r.url().host(), mode / 2 == 1 ? "interfacepc.music.163.com" : "music.163.com");
            QVERIFY(r.rawHeader("Cookie").contains("MUSIC_U=synthetic-original"));
            QVERIFY(!r.rawHeader("Cookie").contains("synthetic-rotation"));
            // Only the heart says which client it is (-460 otherwise).
            QCOMPARE(r.rawHeader("Cookie").contains("os=pc; appver=3.1.17.204416"),
                     operations[mode] == ApiOperation::SongHeart);
            if (mode / 2 == 1) {
                const auto wire = decodeEapiRequest(h.http->bodies.last());
                QVERIFY(wire);
                QCOMPARE(wire->logicalPath, paths[mode]);
                auto payload = wire->payload;
                QVERIFY(payload.take("header").isObject());
                QCOMPARE(payload.take("e_r"), QJsonValue(false));
                QCOMPARE(payload, (QJsonObject{{"pid", "2"},
                                               {"op", mode % 2 ? "del" : "add"},
                                               {"trackIds", "[\"9007199254740993\",\"4\"]"},
                                               {"imme", "true"}}));
            } else {
                QVERIFY(h.http->bodies.last().startsWith("params="));
                QVERIFY(h.http->bodies.last().contains("&encSecKey="));
            }
            QCOMPARE(h.http->requests.size(), i + 2); // exactly one explicit mutation; no refresh
        }
    }
    void responses_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<CollectionOutcome>("outcome");
        QTest::addColumn<CollectionError>("error");
        for (int i = 0; i < 10; ++i) {
            const auto prefix = QByteArray(names[i]) + '-';
            QTest::newRow(prefix + "ack")
                << i << QByteArray(R"({"code":200})") << 200 << CollectionOutcome::Acknowledged
                << CollectionError::None;
            QTest::newRow(prefix + "ack-string")
                << i << QByteArray(R"({"code":"200"})") << 200 << CollectionOutcome::Acknowledged
                << CollectionError::None;
            for (const int code : {201, 512, -462, 403})
                QTest::newRow(prefix + QByteArray::number(code))
                    << i << QByteArray("{\"code\":" + QByteArray::number(code) + "}") << 200
                    << CollectionOutcome::Rejected << CollectionError::ProviderRejected;
            for (const QByteArray bad :
                 {QByteArray("{}"), QByteArray("broken"), QByteArray(R"({"code":"2e2"})"),
                  QByteArray(R"({"code":true})"), QByteArray("[]")})
                QTest::newRow(prefix + "schema-" + bad)
                    << i << bad << 200 << CollectionOutcome::Unknown
                    << CollectionError::InvalidResponse;
            QTest::newRow(prefix + "business429")
                << i << QByteArray(R"({"code":429})") << 200 << CollectionOutcome::Rejected
                << CollectionError::RateLimited;
            QTest::newRow(prefix + "http429")
                << i << QByteArray("{}") << 429 << CollectionOutcome::Unknown
                << CollectionError::RateLimited;
            QTest::newRow(prefix + "http502")
                << i << QByteArray(R"({"code":200})") << 502 << CollectionOutcome::Unknown
                << CollectionError::Network;
        }
    }
    void responses()
    {
        QFETCH(int, mode);
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(CollectionOutcome, outcome);
        QFETCH(CollectionError, error);
        Harness h({Plan{body, status}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        QVERIFY(h.submit(mutation(mode)));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, outcome);
        QCOMPARE(result(done).error, error);
        if (error == CollectionError::RateLimited)
            QCOMPARE(result(done).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(1);
        QCOMPARE(h.http->requests.size(), 2);
        QCOMPARE(done.size(), 1);
    }
    void gates_data() { contracts_data(); }
    void gates()
    {
        QFETCH(int, mode);
        Harness guest({}, false);
        QSignalSpy g(&guest.gateway, &ICollectionGateway::completed);
        QVERIFY(guest.submit(mutation(mode)));
        QTRY_COMPARE(g.size(), 1);
        QCOMPARE(result(g).outcome, CollectionOutcome::NotSent);
        QCOMPARE(result(g).error, CollectionError::AuthenticationRequired);
        QVERIFY(guest.http->requests.isEmpty());
        Harness missing({}, true, false);
        missing.client.setAnonymousToken("synthetic-anonymous");
        QSignalSpy a(&missing.gateway, &ICollectionGateway::completed);
        QVERIFY(missing.submit(mutation(mode)));
        QTRY_COMPARE(a.size(), 1);
        QCOMPARE(result(a).outcome, CollectionOutcome::NotSent);
        QCOMPARE(result(a).error, CollectionError::AuthenticationRequired);
        QVERIFY(missing.http->requests.isEmpty());
        Harness h({});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        QVERIFY(h.submit(mutation(mode), "202"));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).error, CollectionError::InvalidRequest);
        QCOMPARE(result(done).outcome, CollectionOutcome::NotSent);
        QCOMPARE(h.http->requests.size(), 1);
        const auto token = h.session.snapshot().token;
        h.session.signOut();
        QVERIFY(h.gateway.submit(token, "101", mutation(mode)));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(result(done).error, CollectionError::StaleContext);
        QCOMPARE(result(done).outcome, CollectionOutcome::NotSent);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
    }
    void cancelAndStale_data() { contracts_data(); }
    void cancelAndStale()
    {
        QFETCH(int, mode);
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto id = h.submit(mutation(mode));
        h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CollectionOutcome::NotSent);
        QCOMPARE(result(done).error, CollectionError::Canceled);
        QCOMPARE(h.http->requests.size(), 1);
        const auto dispatched = h.submit(mutation(mode));
        QVERIFY(dispatched);
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.gateway.cancel(dispatched);
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(result(done).error, CollectionError::Canceled);
        const auto token = h.session.snapshot().token;
        QVERIFY(h.submit(mutation(mode)));
        QTRY_COMPARE(h.http->requests.size(), 3);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 3);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(result(done).error, CollectionError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
    }
    void mappingAndBounds()
    {
        ApiResult a;
        a.error = ApiError::Transport;
        a.transportError = TransportError::Timeout;
        a.submittedToTransport = true;
        QCOMPARE(collectionResult(a).outcome, CollectionOutcome::Unknown);
        QCOMPARE(collectionResult(a).error, CollectionError::Timeout);
        a.submittedToTransport = false;
        QCOMPARE(collectionResult(a).outcome, CollectionOutcome::NotSent);
        a.error = ApiError::Crypto;
        a.transportError = TransportError::None;
        QCOMPARE(collectionResult(a).error, CollectionError::ProtocolFailure);
        a.httpStatus = 429;
        for (QByteArray v :
             {QByteArray("3, 3"), QByteArray("date"), QByteArray("-1"), QByteArray("86401")}) {
            a.retryAfter = v;
            QVERIFY(!collectionResult(a).retryAfterMs);
        }
        QList<Plan> plans;
        for (int i = 0; i < 8; ++i)
            plans.append(Plan{"", 200, -1});
        Harness h(plans);
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto m = mutation();
        QVERIFY(h.submit(m));
        QVERIFY(!h.submit(m));
        for (int i = 1; i < 8; ++i)
            QVERIFY(h.submit(mutation(i)));
        QVERIFY(!h.submit());
        QTRY_COMPARE(h.http->requests.size(), 9);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 8);
        for (const auto& row : done)
            QCOMPARE(qvariant_cast<CollectionResult>(row[2]).outcome, CollectionOutcome::Unknown);
    }
    void lostClient()
    {
        SessionCoordinator session;
        const auto attempt = session.beginAuthentication();
        const auto prepared = session.prepareAuthentication(attempt, "101");
        QVERIFY(prepared);
        QVERIFY(session.completeAuthentication(*prepared));
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        FakeManager* http = nullptr;
        auto client = std::make_unique<SessionApiClient>(
            Harness::transport({seed, Plan{"", 200, -1}}, http), "synthetic-collection");
        const auto token = session.snapshot().token;
        QVERIFY(client->advanceTo(token));
        QSignalSpy seeded(client.get(), &SessionApiClient::completed);
        QVERIFY(client->submit(token, ApiOperation::QrKey));
        QTRY_COMPARE(seeded.size(), 1);
        CollectionGateway gateway(session, *client);
        QSignalSpy done(&gateway, &ICollectionGateway::completed);
        const auto m = mutation();
        QVERIFY(gateway.submit(token, "101", m));
        QTRY_COMPARE(http->requests.size(), 2);
        client.reset();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).eventId, m.eventId);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(result(done).error, CollectionError::Network);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        QVERIFY(gateway.submit(token, "101", mutation()));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(result(done).outcome, CollectionOutcome::NotSent);
        QCOMPARE(result(done).error, CollectionError::Network);
    }
    void facadeSameTokenBarriers()
    {
        for (int i = 0; i < 3; ++i) {
            BackendFacade f;
            ready(f);
            QSignalSpy done(&f, &BackendFacade::collectionCompleted),
                req(&f, &BackendFacade::collectionRequested);
            const auto id = f.mutateCollection(mutation());
            QVERIFY(id);
            const auto command = qvariant_cast<CollectionCommand>(req.last()[0]);
            BackendEvent event;
            event.kind = BackendEvent::Kind::Session;
            event.session = f.snapshot();
            if (i == 0)
                event.session.phase = SessionPhase::Guest;
            else if (i == 1)
                event.session.phase = SessionPhase::Committing;
            else
                event.session.accountId = "202";
            f.deliver(event);
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
            QCOMPARE(result(done).error, CollectionError::StaleContext);
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), command.context);
            f.deliverCollection(
                {id, command.generation, command.context, {{}, CollectionOutcome::Acknowledged}});
            QCOMPARE(done.size(), 1);
        }
    }
    void facade()
    {
        BackendFacade f;
        QVERIFY(!f.mutateCollection(mutation()));
        ready(f);
        QSignalSpy done(&f, &BackendFacade::collectionCompleted),
            requests(&f, &BackendFacade::collectionRequested),
            music(&f, &BackendFacade::musicRequested), play(&f, &BackendFacade::playbackRequested),
            report(&f, &BackendFacade::listeningReportRequested);
        const auto token = f.snapshot().token;
        const auto m = mutation();
        const auto id = f.mutateCollection(m);
        QVERIFY(id);
        QVERIFY(!f.mutateCollection(m));
        const auto command = qvariant_cast<CollectionCommand>(requests.last()[0]);
        QCOMPARE(command.accountId, "101");
        f.deliverCollection({id,
                             command.generation,
                             token,
                             {QUuid::createUuid(), CollectionOutcome::Acknowledged}});
        QCOMPARE(done.size(), 1);
        QCOMPARE(result(done).eventId, m.eventId);
        QCOMPARE(result(done).outcome, CollectionOutcome::Acknowledged);
        QVERIFY(f.mutateCollection(m)); // completed UUID is NOT remote deduplication
        for (int i = 0; i < 7; ++i)
            QVERIFY(f.mutateCollection(mutation(i)));
        QVERIFY(!f.mutateCollection(mutation()));
        QVERIFY(f.signOut());
        QVERIFY(!f.snapshot().token.isValid());
        QVERIFY(!f.mutateCollection(mutation()));
        QTRY_COMPARE(done.size(), 9);
        for (int i = 1; i < done.size(); ++i) {
            const auto r = qvariant_cast<CollectionResult>(done[i][2]);
            QCOMPARE(r.outcome, CollectionOutcome::Unknown);
            QCOMPARE(r.error, CollectionError::StaleContext);
            QCOMPARE(qvariant_cast<SessionToken>(done[i][1]), token);
        }
        for (const auto& row : requests) {
            const auto c = qvariant_cast<CollectionCommand>(row[0]);
            f.deliverCollection({c.id,
                                 c.generation,
                                 c.context,
                                 {c.mutation.eventId, CollectionOutcome::Acknowledged}});
        }
        QCOMPARE(done.size(), 9);
        QVERIFY(music.isEmpty());
        QVERIFY(play.isEmpty());
        QVERIFY(report.isEmpty());
    }
    void facadeCancellationAndForgery()
    {
        BackendFacade f;
        ready(f);
        QSignalSpy done(&f, &BackendFacade::collectionCompleted),
            req(&f, &BackendFacade::collectionRequested);
        auto id = f.mutateCollection(mutation());
        QVERIFY(id);
        f.cancelCollection(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(result(done).error, CollectionError::Canceled);
        id = f.mutateCollection(mutation());
        QVERIFY(id);
        const auto c = qvariant_cast<CollectionCommand>(req.last()[0]);
        f.deliverCollection(
            {id, c.generation + 1, c.context, {{}, CollectionOutcome::Acknowledged}});
        QCOMPARE(done.size(), 2);
        QCOMPARE(result(done).error, CollectionError::StaleContext);
        QCOMPARE(result(done).eventId, c.mutation.eventId);
        id = f.mutateCollection(mutation());
        QVERIFY(id);
        f.close();
        QTRY_COMPARE(done.size(), 3);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(result(done).error, CollectionError::Canceled);
    }
};
QTEST_GUILESS_MAIN(CollectionsTest)
#include "collections_test.moc"
