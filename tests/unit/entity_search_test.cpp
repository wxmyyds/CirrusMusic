#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/entity_search_mapper.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
#include "infrastructure/netease/protocol/eapi_request_profile.h"
#include "infrastructure/netease/protocol/openssl_primitives.h"
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
QList<MusicQuery> queries()
{
    const SearchPage page{" synthetic ", 2, 0};
    return {EntitySearchQuery{MusicSearchKind::Album, page},
            EntitySearchQuery{MusicSearchKind::Artist, page},
            EntitySearchQuery{MusicSearchKind::Playlist, page}, PodcastSearchQuery{page},
            VoiceSearchQuery{page}};
}
// Schema-shape fixtures are synthetic, not retained provider records.
QList<QByteArray> bodies()
{
    return {
        R"({"code":200,"result":{"albums":[{"id":"9007199254740993","name":"synthetic album","artists":[{"id":2,"name":"synthetic"}]}],"albumCount":3}})",
        R"({"code":200,"result":{"artists":[{"id":2,"name":"synthetic artist","albumSize":0}],"artistCount":4,"hasMore":true}})",
        R"({"code":200,"result":{"playlists":[{"id":3,"name":"synthetic playlist","playCount":1.5}],"playlistCount":5,"hasMore":false}})",
        R"({"code":200,"data":{"resources":[{"resourceType":"voicelist","resourceId":"4","baseInfo":{"id":4,"name":"synthetic podcast","programCount":2}}],"totalCount":6,"hasMore":true}})",
        R"({"code":200,"data":{"resources":[{"resourceType":"voice","resourceId":"5","baseInfo":{"id":5,"name":"synthetic voice","duration":2000,"radio":{"id":4,"name":"synthetic podcast"},"mainSong":{"id":99},"mainTrackId":99}}],"totalCount":7,"hasMore":true}})"};
}
QJsonObject json(const QByteArray& b) { return QJsonDocument::fromJson(b).object(); }
MusicResult map(int index, const QJsonObject& o) { return mapMusicResponse(queries()[index], o); }
QByteArray encrypted(const QByteArray& b)
{
    const auto a = cirrus::netease::detail::aes(b, "e82ckenh8dichen8", EVP_aes_128_ecb(), true);
    return a.value_or(QByteArray{});
}
MusicResult result(const QSignalSpy& spy) { return qvariant_cast<MusicResult>(spy.last()[2]); }
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    MusicGateway gateway{client};
    MusicQueryService service{session, gateway};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& out)
    {
        auto m = std::make_unique<FakeManager>();
        out = m.get();
        m->plans = plans;
        return std::make_unique<HttpTransport>(std::move(m));
    }
    explicit Harness(QList<Plan> plans, bool auth = false)
        : client(transport(plans, http), "synthetic-search-device")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto p = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(p);
        QVERIFY(session.completeAuthentication(*p));
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans.prepend(seed);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
};
} // namespace
class EntitySearchTest : public QObject
{
    Q_OBJECT
  private slots:
    void typedResultsAndNamespaces()
    {
        static_assert(!std::is_same_v<VoiceId, SongId>);
        static_assert(!std::is_same_v<PodcastId, PlaylistId>);
        const auto album = map(0, json(bodies()[0]));
        QVERIFY(std::holds_alternative<AlbumSearchPage>(album));
        const auto a = std::get<AlbumSearchPage>(album);
        QCOMPARE(a.albums.first().id.value, "9007199254740993");
        QCOMPARE(a.query.page.keywords, QString(" synthetic "));
        QCOMPARE(a.total, std::optional<qint64>(3));
        QVERIFY(!a.hasMore);
        QVERIFY(!a.albums.first().trackCount);
        const auto artist = map(1, json(bodies()[1]));
        QVERIFY(std::holds_alternative<ArtistSearchPage>(artist));
        const auto ar = std::get<ArtistSearchPage>(artist).artists.first();
        QCOMPARE(ar.albumCount, std::optional<qint64>(0));
        QVERIFY(!ar.songCount);
        const auto playlist = map(2, json(bodies()[2]));
        QVERIFY(std::holds_alternative<PlaylistSearchPage>(playlist));
        QCOMPARE(std::get<PlaylistSearchPage>(playlist).playlists.first().playCount,
                 std::optional<double>(1.5));
        const auto podcast = map(3, json(bodies()[3]));
        QVERIFY(std::holds_alternative<PodcastSearchPage>(podcast));
        QCOMPARE(std::get<PodcastSearchPage>(podcast).podcasts.first().id.value, "4");
        const auto voice = map(4, json(bodies()[4]));
        QVERIFY(std::holds_alternative<VoiceSearchPage>(voice));
        const auto v = std::get<VoiceSearchPage>(voice).voices.first();
        QCOMPARE(v.id.value, "5");
        QVERIFY(v.podcast);
        QCOMPARE(v.podcast->id.value, "4");
        QCOMPARE(v.providerDuration, std::optional<qint64>(2000));
        QCOMPARE(v.track->value, "99"); // mainSong.id: the audio (a voice is a program)
        const auto unknown = map(
            4,
            json(
                R"({"code":200,"data":{"resources":[{"baseInfo":{"id":5}},{"baseInfo":{"id":5}}]}})"));
        QVERIFY(std::holds_alternative<VoiceSearchPage>(unknown));
        const auto page = std::get<VoiceSearchPage>(unknown);
        QCOMPARE(page.voices.size(), 2);
        QVERIFY(!page.total);
        QVERIFY(!page.hasMore);
        QVERIFY(!page.voices.first().podcast);
        QVERIFY(!page.voices.first().providerDuration);
    }
    void payloadsAndClosedProfiles()
    {
        const QStringList paths{"/api/cloudsearch/pc", "/api/cloudsearch/pc", "/api/cloudsearch/pc",
                                "/api/search/voicelist/get", "/api/search/voice/get"};
        for (int i = 0; i < 5; ++i) {
            const auto q = queries()[i];
            QVERIFY(isEntitySearchQuery(q));
            QVERIFY(!requiresAuthenticatedMusicQuery(q));
            QVERIFY(!musicQueryAccount(q));
            const auto r = entitySearchRequest(q);
            QVERIFY(r);
            const auto c = operationContract(r->operation);
            QVERIFY(c);
            QCOMPARE(c->logicalPath, paths[i]);
            QCOMPARE(c->protocol, Protocol::Eapi);
            QCOMPARE(c->responseLimit, 2 * 1024 * 1024);
            QCOMPARE(c->responseEncoding,
                     i == 3 ? ResponseEncoding::EapiAutoJson : ResponseEncoding::Json);
            QVERIFY(!requiresAccountCookie(r->operation));
            const auto p = operationPayload(r->operation, r->parameters);
            QVERIFY(p);
            QCOMPARE(p->value("limit"), QJsonValue(2));
            QCOMPARE(p->value("offset"), QJsonValue(0));
            if (i < 3) {
                QCOMPARE(p->value("s"), QJsonValue("synthetic"));
                QCOMPARE(p->value("type").toInt(), i == 0 ? 10 : i == 1 ? 100 : 1000);
                QVERIFY(!p->contains("keyword"));
            } else {
                QCOMPARE(p->value("keyword"), QJsonValue("synthetic"));
                QCOMPARE(p->value("scene"), QJsonValue("normal"));
                QVERIFY(!p->contains("radioId"));
                QVERIFY(!p->contains("type"));
            }
            if (i == 3)
                QCOMPARE(p->value("e_r"), QJsonValue(true));
            for (const QString key : {"host", "type", "radioId", "voiceListId", "e_r"}) {
                auto extra = r->parameters;
                extra.insert(key, "injected");
                QVERIFY(!operationPayload(r->operation, extra));
            }
            auto bad = r->parameters;
            bad.insert("limit", true);
            QVERIFY(!operationPayload(r->operation, bad));
            bad = r->parameters;
            bad.insert("offset", 1.5);
            QVERIFY(!operationPayload(r->operation, bad));
            EapiRequestMetadata m{"synthetic", 1000000, 1};
            m.encryptedResponse = true;
            QCOMPARE(prepareEapiRequest(c->logicalPath, *p, m).ok(), i == 3);
        }
        QCOMPARE(PodcastSearchQuery{}.page.limit, 10);
        QCOMPARE(PodcastSearchQuery{}.page.offset, 0);
        QVERIFY((!EntitySearchQuery{static_cast<MusicSearchKind>(3), {"ok"}}.isValid()));
        for (const SearchPage p :
             {SearchPage{""}, SearchPage{"   "}, SearchPage{QString(257, u'x')},
              SearchPage{QString(QChar::Null)}, SearchPage{QString(QChar(0xd800))},
              SearchPage{"ok", 0, 0}, SearchPage{"ok", 101, 0}, SearchPage{"ok", 1, -1},
              SearchPage{"ok", 1, 10001}}) {
            QVERIFY(!p.isValid());
            QVERIFY(!entitySearchRequest(VoiceSearchQuery{p}));
        }
    }
    void emptyAndBounds()
    {
        for (int i = 0; i < 5; ++i) {
            auto o = json(bodies()[i]);
            auto data = o.value(i < 3 ? "result" : "data").toObject();
            const QString list = i == 0   ? "albums"
                                 : i == 1 ? "artists"
                                 : i == 2 ? "playlists"
                                          : "resources";
            const QString count = i == 0   ? "albumCount"
                                  : i == 1 ? "artistCount"
                                  : i == 2 ? "playlistCount"
                                           : "totalCount";
            data.insert(list, QJsonArray{});
            data.insert(count, 0);
            data.insert("hasMore", false);
            o.insert(i < 3 ? "result" : "data", data);
            QVERIFY(!std::holds_alternative<MusicFailure>(map(i, o)));
            if (i >= 3) {
                data.insert(list, QJsonValue::Null);
                o.insert("data", data);
                QVERIFY(!std::holds_alternative<MusicFailure>(map(i, o)));
                data.remove(count);
                o.insert("data", data);
                QVERIFY(std::holds_alternative<MusicFailure>(map(i, o)));
            }
            o = json(bodies()[i]);
            data = o.value(i < 3 ? "result" : "data").toObject();
            auto values = data.value(list).toArray();
            values.append(values.first());
            values.append(values.first());
            data.insert(list, values);
            o.insert(i < 3 ? "result" : "data", data);
            QVERIFY(std::holds_alternative<MusicFailure>(map(i, o)));
        }
        auto o = json(bodies()[4]);
        auto data = o["data"].toObject();
        auto values = data["resources"].toArray();
        auto row = values.first().toObject();
        auto base = row["baseInfo"].toObject();
        base.insert("id", "9007199254740993");
        row.insert("resourceId", "9007199254740993");
        row.insert("baseInfo", base);
        data.insert("resources", QJsonArray{row});
        o.insert("data", data);
        const auto v = map(4, o);
        QVERIFY(std::holds_alternative<VoiceSearchPage>(v));
        QCOMPARE(std::get<VoiceSearchPage>(v).voices.first().id.value, "9007199254740993");
    }
    void badSchema_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("missing-albums")
            << 0 << QByteArray(R"({"code":200,"result":{"albumCount":0}})");
        QTest::newRow("unsafe-id")
            << 0 << QByteArray(R"({"code":200,"result":{"albums":[{"id":9007199254740993}]}})");
        QTest::newRow("bad-artist-name")
            << 1 << QByteArray(R"({"code":200,"result":{"artists":[{"id":1,"name":true}]}})");
        QTest::newRow("bad-artist-size")
            << 1 << QByteArray(R"({"code":200,"result":{"artists":[{"id":1,"albumSize":-1}]}})");
        QTest::newRow("bad-more") << 1
                                  << QByteArray(
                                         R"({"code":200,"result":{"artists":[],"hasMore":1}})");
        QTest::newRow("bad-count")
            << 1 << QByteArray(R"({"code":200,"result":{"artists":[],"artistCount":1.5}})");
        QTest::newRow("contradictory-count")
            << 2
            << QByteArray(R"({"code":200,"result":{"playlists":[{"id":1}],"playlistCount":0}})");
        QTest::newRow("bad-playcount")
            << 2
            << QByteArray(R"({"code":200,"result":{"playlists":[{"id":1,"playCount":true}]}})");
        QTest::newRow("bad-trackcount")
            << 2 << QByteArray(R"({"code":200,"result":{"playlists":[{"id":1,"trackCount":-1}]}})");
        QTest::newRow("wrong-podcast-root")
            << 3 << QByteArray(R"({"code":200,"result":{"djRadios":[]}})");
        QTest::newRow("wrong-podcast-resource")
            << 3
            << QByteArray(
                   R"({"code":200,"data":{"resources":[{"resourceType":"voice","baseInfo":{"id":1}}]}})");
        QTest::newRow("wrong-voice-resource")
            << 4
            << QByteArray(
                   R"({"code":200,"data":{"resources":[{"resourceType":"voicelist","baseInfo":{"id":1}}]}})");
        QTest::newRow("resource-id-mismatch")
            << 4
            << QByteArray(
                   R"({"code":200,"data":{"resources":[{"resourceId":"2","baseInfo":{"id":1}}]}})");
        QTest::newRow("no-base")
            << 4
            << QByteArray(R"({"code":200,"data":{"resources":[{"id":1,"mainSong":{"id":2}}]}})");
        QTest::newRow("wrong-base")
            << 4 << QByteArray(R"({"code":200,"data":{"resources":[{"baseInfo":[]}]}})");
        QTest::newRow("missing-data") << 4 << QByteArray(R"({"code":200,"result":{"songs":[]}})");
        QTest::newRow("unknown-null-resources")
            << 4 << QByteArray(R"({"code":200,"data":{"resources":null}})");
        QTest::newRow("inconsistent-null-more")
            << 4
            << QByteArray(
                   R"({"code":200,"data":{"resources":null,"totalCount":0,"hasMore":true}})");
        QTest::newRow("bad-duration")
            << 4
            << QByteArray(
                   R"({"code":200,"data":{"resources":[{"baseInfo":{"id":1,"duration":-1}}]}})");
        QTest::newRow("bad-radio")
            << 4
            << QByteArray(R"({"code":200,"data":{"resources":[{"baseInfo":{"id":1,"radio":1}}]}})");
        QTest::newRow("bad-radio-id")
            << 4
            << QByteArray(
                   R"({"code":200,"data":{"resources":[{"baseInfo":{"id":1,"radio":{"id":0}}}]}})");
        QTest::newRow("negative-total")
            << 3 << QByteArray(R"({"code":200,"data":{"resources":[],"totalCount":-1}})");
        QTest::newRow("text-total")
            << 4 << QByteArray(R"({"code":200,"data":{"resources":[],"totalCount":"1"}})");
    }
    void badSchema()
    {
        QFETCH(int, index);
        QFETCH(QByteArray, body);
        const auto r = map(index, json(body));
        QVERIFY(std::holds_alternative<MusicFailure>(r));
        QCOMPARE(std::get<MusicFailure>(r).error, MusicError::InvalidResponse);
    }
    void nativeChain_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<bool>("auth");
        for (int i = 0; i < 5; ++i)
            for (bool auth : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(i).arg(auth))) << i << auth;
    }
    void nativeChain()
    {
        QFETCH(int, index);
        QFETCH(bool, auth);
        Plan first{index == 3 ? encrypted(bodies()[index]) : bodies()[index]};
        first.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({first, first}, auth);
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        QVERIFY(h.service.request(token, queries()[index]));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!std::holds_alternative<MusicFailure>(result(done)));
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        const auto r = *entitySearchRequest(queries()[index]);
        const auto c = *operationContract(r.operation);
        const auto wire = decodeEapiRequest(h.http->bodies.last());
        QVERIFY(wire);
        QCOMPARE(wire->logicalPath, c.logicalPath);
        QCOMPARE(h.http->requests.last().url().host(), "interfacepc.music.163.com");
        QCOMPARE(h.http->requests.last().url().path(), "/eapi" + c.logicalPath.mid(4));
        auto business = wire->payload;
        business.remove("header");
        QCOMPARE(business.value("e_r"), QJsonValue(index == 3));
        business.remove("e_r");
        auto p = *operationPayload(r.operation, r.parameters);
        p.remove("e_r");
        QCOMPARE(business, p);
        QVERIFY(h.service.request(token, queries()[index]));
        QTRY_COMPARE(done.size(), 2);
        const auto cookie = h.http->requests.last().rawHeader("Cookie");
        QVERIFY(!cookie.contains("synthetic-rotation"));
        if (auth)
            QVERIFY(cookie.contains("MUSIC_U=synthetic-original"));
        else {
            QVERIFY(!cookie.contains("MUSIC_U="));
            QVERIFY(!cookie.contains("MUSIC_A="));
        }
        QCOMPARE(h.http->requests.size(), auth ? 3 : 2);
    }
    void cancelStaleRate_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < 5; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void cancelStaleRate()
    {
        QFETCH(int, index);
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.service.request(token, queries()[index]);
        QVERIFY(id);
        QTRY_COMPARE(h.http->requests.size(), 1);
        h.service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(h.service.request(token, queries()[index]));
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        Harness rate({Plan{"", 429}});
        QSignalSpy d(&rate.service, &MusicQueryService::completed);
        QVERIFY(rate.service.request(rate.session.snapshot().token, queries()[index]));
        QTRY_COMPARE(d.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(d)).error, MusicError::RateLimited);
        QCOMPARE(std::get<MusicFailure>(result(d)).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(10);
        QCOMPARE(rate.http->requests.size(), 1);
    }
    void facadeMailbox()
    {
        BackendFacade f;
        QVERIFY(!f.searchVoices({"synthetic"}));
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        const SessionToken token{QUuid::createUuid(), 1};
        e.session = {token, SessionPhase::Guest};
        f.deliver(e);
        QSignalSpy requests(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
            reports(&f, &BackendFacade::listeningReportRequested);
        QVERIFY(f.searchAlbums({"synthetic"}));
        QVERIFY(f.searchArtists({"synthetic"}));
        QVERIFY(f.searchPlaylists({"synthetic"}));
        QVERIFY(f.searchPodcasts({"synthetic"}));
        QVERIFY(f.searchVoices({"synthetic"}));
        QVERIFY(!f.searchVoices({""}));
        QVERIFY(!f.searchPodcasts({"ok", 101, 0}));
        const auto old = qvariant_cast<MusicCommand>(requests.first()[0]);
        for (int i = 0; i < 11; ++i)
            QVERIFY(f.searchVoices({"synthetic", 1, 0}));
        QVERIFY(!f.searchVoices({"synthetic"}));
        QVERIFY(f.signOut());
        QTRY_COMPARE(done.size(), 16);
        f.deliverMusic({old.id, old.generation, token,
                        AlbumSearchPage{std::get<EntitySearchQuery>(old.query)}});
        QCOMPARE(done.size(), 16);
        QVERIFY(play.isEmpty());
        QVERIFY(reports.isEmpty());
    }
};
QTEST_GUILESS_MAIN(EntitySearchTest)
#include "entity_search_test.moc"
