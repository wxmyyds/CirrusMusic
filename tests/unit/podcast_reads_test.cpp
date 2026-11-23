#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
#include "infrastructure/netease/music/podcast_mapper.h"
#include "infrastructure/netease/music/recent_entity_mapper.h"
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
    return {PodcastDetailQuery{{"4"}},
            PodcastProgramsQuery{{"4"}, {2, 0}},
            ProgramDetailQuery{{"5"}},
            RecentEntitiesQuery{"101", RecentEntityKind::Playlist, 2},
            RecentEntitiesQuery{"101", RecentEntityKind::Album, 2},
            RecentEntitiesQuery{"101", RecentEntityKind::Podcast, 2},
            RecentEntitiesQuery{"101", RecentEntityKind::Voice, 2}};
}
QList<QByteArray> bodies()
{
    // Synthetic schema records only; public probe retained shapes, never creator/audio/account
    // data.
    return {
        R"({"code":200,"data":{"id":4,"name":"synthetic podcast","desc":"synthetic","programCount":2}})",
        R"({"code":200,"programs":[{"id":5,"name":"synthetic program","radio":{"id":4},"duration":2000,"serialNum":0,"mainSong":{"id":99},"mainTrackId":99}],"count":3,"more":true})",
        R"({"code":200,"program":{"id":5,"name":"synthetic program","radio":{"id":4},"duration":2000,"mainSong":{"id":99}}})",
        R"({"code":200,"uid":101,"data":{"list":[{"resourceId":"3","resourceType":"PLAYLIST","playTime":1730000000000,"data":{"id":3,"name":"synthetic playlist","playCount":1.5}}],"total":5}})",
        R"({"code":200,"data":{"list":[{"resourceId":"2","resourceType":"ALBUM","playTime":0,"data":{"id":2,"name":"synthetic album","size":0}}]}})",
        R"({"code":200,"data":{"list":[{"resourceId":"4","resourceType":"DJRADIO","data":{"id":4,"name":"synthetic podcast","programCount":0}}]}})",
        R"({"code":200,"data":{"list":[{"resourceId":"5","resourceType":"VOICE","data":{"id":5,"name":"synthetic voice","radio":{"id":4},"mainSong":{"id":99},"duration":2000}}]}})"};
}
QJsonObject json(const QByteArray& b) { return QJsonDocument::fromJson(b).object(); }
MusicResult mapped(int i, const QByteArray& b) { return mapMusicResponse(queries()[i], json(b)); }
MusicResult result(const QSignalSpy& s) { return qvariant_cast<MusicResult>(s.last()[2]); }
ApiOperation operation(int i)
{
    return i < 3 ? podcastRequest(queries()[i])->operation
                 : recentEntityRequest(queries()[i])->operation;
}
QJsonObject parameters(int i)
{
    return i < 3 ? podcastRequest(queries()[i])->parameters
                 : recentEntityRequest(queries()[i])->parameters;
}
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
    Harness(QList<Plan> plans, bool auth = true, bool cookie = true)
        : client(transport(plans, http), "synthetic-podcast-reads")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto p = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(p);
        QVERIFY(session.completeAuthentication(*p));
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
};
} // namespace
class PodcastReadsTest : public QObject
{
    Q_OBJECT
  private slots:
    void contracts()
    {
        static_assert(!std::is_same_v<ProgramId, VoiceId>);
        static_assert(!std::is_same_v<ProgramId, SongId>);
        static_assert(!std::is_same_v<PodcastId, PlaylistId>);
        const QStringList paths{"/api/djradio/v2/get",         "/api/dj/program/byradio",
                                "/api/dj/program/detail",      "/api/play-record/playlist/list",
                                "/api/play-record/album/list", "/api/play-record/djradio/list",
                                "/api/play-record/voice/list"};
        for (int i = 0; i < 7; ++i) {
            const auto q = queries()[i];
            const auto op = operation(i);
            const auto p = parameters(i);
            const auto c = operationContract(op);
            QVERIFY(c);
            QCOMPARE(c->logicalPath, paths[i]);
            QCOMPARE(c->protocol, Protocol::Weapi);
            QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
            QCOMPARE(c->responseLimit, (i == 0 || i == 2 ? 2 : 4) * 1024 * 1024);
            QCOMPARE(c->destination, OfficialDestination::Default);
            QCOMPARE(requiresAccountCookie(op), i >= 3);
            QCOMPARE(requiresAuthenticatedMusicQuery(q), i >= 3);
            QCOMPARE(musicQueryAccount(q), i >= 3 ? std::optional<QString>("101") : std::nullopt);
            QVERIFY(!libraryQueryAccount(q));
            const auto payload = operationPayload(op, p);
            QVERIFY(payload);
            QCOMPARE(*payload, p);
            if (i >= 3) {
                QCOMPARE(payload->size(), 1);
                QVERIFY(!payload->contains("uid"));
            }
            for (const QString name :
                 {"host", "offsetExtra", "uid", "csrf_token", "e_r", "voiceId"}) {
                auto bad = p;
                bad.insert(name, "injected");
                QVERIFY(!operationPayload(op, bad));
            }
            if (i >= 3) {
                for (const QJsonValue v : {QJsonValue(0), QJsonValue(101), QJsonValue(1.5),
                                           QJsonValue(true), QJsonValue("2")})
                    QVERIFY(!operationPayload(op, {{"limit", v}}));
            }
        }
        QCOMPARE(PodcastProgramsQuery{}.page.limit, 30);
        QCOMPARE(PodcastProgramsQuery{}.page.offset, 0);
        QCOMPARE(RecentEntitiesQuery{}.limit, 100);
        QVERIFY((!PodcastDetailQuery{{"01"}}.isValid()));
        QVERIFY((!ProgramDetailQuery{{"0"}}.isValid()));
        QVERIFY((!PodcastProgramsQuery{{"4"}, {2, 0}, static_cast<ProgramOrder>(2)}.isValid()));
        QVERIFY((!RecentEntitiesQuery{"101", static_cast<RecentEntityKind>(4), 2}.isValid()));
        QVERIFY((!RecentEntitiesQuery{"", RecentEntityKind::Voice, 2}.isValid()));
        auto p = parameters(1);
        p.insert("asc", 1);
        QVERIFY(!operationPayload(operation(1), p));
        p = parameters(1);
        p.insert("offset", 1.5);
        QVERIFY(!operationPayload(operation(1), p));
        p = parameters(1);
        p.insert("limit", true);
        QVERIFY(!operationPayload(operation(1), p));
        const auto oldest = podcastRequest(
            PodcastProgramsQuery{{"9007199254740993"}, {100, 100000}, ProgramOrder::OldestFirst});
        QVERIFY(oldest);
        QCOMPARE(oldest->parameters.value("asc"), QJsonValue(true));
        QVERIFY(operationPayload(oldest->operation, oldest->parameters));
    }
    void typedValues()
    {
        const auto pod = mapped(0, bodies()[0]);
        QVERIFY(std::holds_alternative<PodcastDetails>(pod));
        QCOMPARE(std::get<PodcastDetails>(pod).podcast.id.value, "4");
        const auto list = mapped(1, bodies()[1]);
        QVERIFY(std::holds_alternative<PodcastProgramsPage>(list));
        const auto page = std::get<PodcastProgramsPage>(list);
        QCOMPARE(page.programs.first().id.value, "5");
        QCOMPARE(page.programs.first().serialNumber, std::optional<qint64>(0));
        QCOMPARE(page.programs.first().track, std::optional<SongId>(SongId{"99"}));
        QCOMPARE(page.total, std::optional<qint64>(3));
        QCOMPARE(page.hasMore, std::optional<bool>(true));
        QCOMPARE(page.query.podcast.value, "4");
        QVERIFY(page.programs.first().podcast);
        const auto detail = mapped(2, bodies()[2]);
        QVERIFY(std::holds_alternative<ProgramDetails>(detail));
        QCOMPARE(std::get<ProgramDetails>(detail).program.id.value, "5");
        QCOMPARE(std::get<ProgramDetails>(detail).program.track->value, "99");
        const auto pl = mapped(3, bodies()[3]);
        QVERIFY(std::holds_alternative<CloudRecentPlaylists>(pl));
        const auto playlists = std::get<CloudRecentPlaylists>(pl);
        QCOMPARE(playlists.items.first().entity.id.value, "3");
        QCOMPARE(playlists.items.first().providerPlayTime, std::optional<qint64>(1730000000000LL));
        QCOMPARE(playlists.items.first().entity.playCount, std::optional<double>(1.5));
        QCOMPARE(playlists.total, std::optional<qint64>(5));
        const auto a = mapped(4, bodies()[4]);
        QVERIFY(std::holds_alternative<CloudRecentAlbums>(a));
        QCOMPARE(std::get<CloudRecentAlbums>(a).items.first().providerPlayTime,
                 std::optional<qint64>(0));
        QVERIFY(!std::get<CloudRecentAlbums>(a).total);
        const auto p = mapped(5, bodies()[5]);
        QVERIFY(std::holds_alternative<CloudRecentPodcasts>(p));
        QCOMPARE(std::get<CloudRecentPodcasts>(p).items.first().entity.voiceCount,
                 std::optional<qint64>(0));
        QVERIFY(!std::get<CloudRecentPodcasts>(p).items.first().providerPlayTime);
        const auto v = mapped(6, bodies()[6]);
        QVERIFY(std::holds_alternative<CloudRecentVoices>(v));
        const auto voice = std::get<CloudRecentVoices>(v).items.first().entity;
        QCOMPARE(voice.id.value, "5");
        QVERIFY(voice.podcast);
        QCOMPARE(voice.providerDuration, std::optional<qint64>(2000));
        QCOMPARE(voice.track->value, "99");
        const auto unknown = mapped(2, R"({"code":200,"program":{"id":5}})");
        QVERIFY(std::holds_alternative<ProgramDetails>(unknown));
        const auto program = std::get<ProgramDetails>(unknown).program;
        QVERIFY(!program.podcast);
        QVERIFY(!program.providerDuration);
        QVERIFY(!program.description);
        QVERIFY(!program.track);
        // mainSong without a usable id: no track, the program itself still reads.
        for (const auto* body :
             {R"({"code":200,"program":{"id":5,"mainSong":{"id":0}}})",
              R"({"code":200,"program":{"id":5,"mainSong":7,"mainTrackId":7}})"}) {
            const auto bad = mapped(2, body);
            QVERIFY(std::holds_alternative<ProgramDetails>(bad));
            QVERIFY(!std::get<ProgramDetails>(bad).program.track);
        }
        const auto wide =
            mapMusicResponse(ProgramDetailQuery{{"9007199254740993"}},
                             json(R"({"code":200,"program":{"id":"9007199254740993"}})"));
        QVERIFY(std::holds_alternative<ProgramDetails>(wide));
    }
    void emptyDuplicatesAndLimits()
    {
        for (int i = 1; i < 7; ++i) {
            if (i == 2)
                continue;
            auto o = json(bodies()[i]);
            auto data = i == 1 ? o : o["data"].toObject();
            const QString key = i == 1 ? "programs" : "list";
            auto rows = data[key].toArray();
            rows.append(rows.first());
            data.insert(key, rows);
            if (i == 1)
                o = data;
            else
                o.insert("data", data);
            QVERIFY(!std::holds_alternative<MusicFailure>(mapMusicResponse(queries()[i], o)));
            rows.append(rows.first());
            data.insert(key, rows);
            if (i == 1)
                o = data;
            else
                o.insert("data", data);
            QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(queries()[i], o)));
            data.insert(key, QJsonArray{});
            data.insert(i == 1 ? "count" : "total", 0);
            if (i == 1)
                o = data;
            else
                o.insert("data", data);
            QVERIFY(!std::holds_alternative<MusicFailure>(mapMusicResponse(queries()[i], o)));
            data.insert(key, QJsonValue::Null);
            if (i == 1)
                o = data;
            else
                o.insert("data", data);
            QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(queries()[i], o)));
        }
    }
    void malformed_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("legacy-radio-wrapper")
            << 0 << QByteArray(R"({"code":200,"djRadio":{"id":4}})");
        QTest::newRow("wrong-radio-id") << 0 << QByteArray(R"({"code":200,"data":{"id":6}})");
        QTest::newRow("podcast-title-type")
            << 0 << QByteArray(R"({"code":200,"data":{"id":4,"name":2}})");
        QTest::newRow("podcast-count")
            << 0 << QByteArray(R"({"code":200,"data":{"id":4,"programCount":-1}})");
        QTest::newRow("wrong-program-wrapper")
            << 2 << QByteArray(R"({"code":200,"data":{"id":5}})");
        QTest::newRow("wrong-program-id") << 2 << QByteArray(R"({"code":200,"program":{"id":99}})");
        QTest::newRow("main-song-not-program")
            << 2 << QByteArray(R"({"code":200,"program":{"mainSong":{"id":5}}})");
        QTest::newRow("unsafe-program-id")
            << 2 << QByteArray(R"({"code":200,"program":{"id":9007199254740993}})");
        QTest::newRow("bad-program-duration")
            << 2 << QByteArray(R"({"code":200,"program":{"id":5,"duration":true}})");
        QTest::newRow("bad-program-description")
            << 2 << QByteArray(R"({"code":200,"program":{"id":5,"description":2}})");
        QTest::newRow("bad-program-parent")
            << 2 << QByteArray(R"({"code":200,"program":{"id":5,"radio":1}})");
        QTest::newRow("mixed-parent")
            << 1 << QByteArray(R"({"code":200,"programs":[{"id":5,"radio":{"id":6}}]})");
        QTest::newRow("more-type") << 1 << QByteArray(R"({"code":200,"programs":[],"more":1})");
        QTest::newRow("count-too-small")
            << 1 << QByteArray(R"({"code":200,"programs":[{"id":5}],"count":0})");
        QTest::newRow("negative-count")
            << 1 << QByteArray(R"({"code":200,"programs":[],"count":-1})");
        QTest::newRow("serial-fraction")
            << 1 << QByteArray(R"({"code":200,"programs":[{"id":5,"serialNum":1.5}]})");
        QTest::newRow("record-wrapper") << 3 << QByteArray(R"({"code":200,"playlist":[{"id":3}]})");
        QTest::newRow("record-data-not-object")
            << 3 << QByteArray(R"({"code":200,"data":{"list":[{"data":1}]}})");
        QTest::newRow("record-id-mismatch")
            << 3
            << QByteArray(R"({"code":200,"data":{"list":[{"resourceId":"2","data":{"id":3}}]}})");
        QTest::newRow("record-type-mismatch")
            << 3
            << QByteArray(
                   R"({"code":200,"data":{"list":[{"resourceType":"ALBUM","data":{"id":3}}]}})");
        QTest::newRow("record-time-negative")
            << 4 << QByteArray(R"({"code":200,"data":{"list":[{"playTime":-1,"data":{"id":2}}]}})");
        QTest::newRow("record-total-fraction")
            << 4 << QByteArray(R"({"code":200,"data":{"list":[],"total":1.5}})");
        QTest::newRow("record-total-small")
            << 5 << QByteArray(R"({"code":200,"data":{"list":[{"data":{"id":4}}],"total":0}})");
        QTest::newRow("record-account-root")
            << 6 << QByteArray(R"({"code":200,"uid":102,"data":{"list":[]}})");
        QTest::newRow("record-account-data")
            << 6 << QByteArray(R"({"code":200,"data":{"uid":102,"list":[]}})");
        QTest::newRow("record-null-list")
            << 6 << QByteArray(R"({"code":200,"data":{"list":null,"total":0}})");
        QTest::newRow("voice-parent-id")
            << 6
            << QByteArray(R"({"code":200,"data":{"list":[{"data":{"id":5,"radio":{"id":0}}}]}})");
        QTest::newRow("voice-song-wrapper")
            << 6 << QByteArray(R"({"code":200,"data":{"list":[{"data":{"mainSong":{"id":5}}}]}})");
        QTest::newRow("voice-parent-type")
            << 6 << QByteArray(R"({"code":200,"data":{"list":[{"data":{"id":5,"radio":[]}}]}})");
        QTest::newRow("playlist-track-fraction")
            << 3
            << QByteArray(R"({"code":200,"data":{"list":[{"data":{"id":3,"trackCount":1.5}}]}})");
    }
    void malformed()
    {
        QFETCH(int, index);
        QFETCH(QByteArray, body);
        QVERIFY(QJsonDocument::fromJson(body).isObject()); // semantic schema cases, not bad JSON
        const auto r = mapped(index, body);
        QVERIFY(std::holds_alternative<MusicFailure>(r));
        QCOMPARE(std::get<MusicFailure>(r).error, MusicError::InvalidResponse);
    }
    void nativeChain_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<bool>("auth");
        for (int i = 0; i < 7; ++i)
            for (bool a : {false, true})
                if (i < 3 || a)
                    QTest::newRow(qPrintable(QString("%1-%2").arg(i).arg(a))) << i << a;
    }
    void nativeChain()
    {
        QFETCH(int, index);
        QFETCH(bool, auth);
        Plan plan{bodies()[index]};
        plan.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({plan, plan}, auth);
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto t = h.session.snapshot().token;
        QVERIFY(h.service.request(t, queries()[index]));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!std::holds_alternative<MusicFailure>(result(done)));
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), t);
        const auto req = h.http->requests.last();
        QCOMPARE(req.url().host(), "music.163.com");
        QCOMPARE(req.url().path(),
                 "/weapi" + operationContract(operation(index))->logicalPath.mid(4));
        QVERIFY(h.http->bodies.last().startsWith("params="));
        QVERIFY(h.http->bodies.last().contains("&encSecKey="));
        QVERIFY(h.service.request(t, queries()[index]));
        QTRY_COMPARE(done.size(), 2);
        QVERIFY(!h.http->requests.last().rawHeader("Cookie").contains("synthetic-rotation"));
        if (auth)
            QVERIFY(h.http->requests.last().rawHeader("Cookie").contains("synthetic-original"));
        QCOMPARE(h.http->requests.size(), auth ? 3 : 2);
    }
    void cancelStaleRate_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < 7; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void cancelStaleRate()
    {
        QFETCH(int, index);
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto t = h.session.snapshot().token;
        auto id = h.service.request(t, queries()[index]);
        QVERIFY(id);
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(h.service.request(t, queries()[index]));
        QTRY_COMPARE(h.http->requests.size(), 3);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), t);
        Harness rate({Plan{"", 429}});
        QSignalSpy r(&rate.service, &MusicQueryService::completed);
        QVERIFY(rate.service.request(rate.session.snapshot().token, queries()[index]));
        QTRY_COMPARE(r.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(r)).error, MusicError::RateLimited);
        QCOMPARE(std::get<MusicFailure>(result(r)).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(10);
        QCOMPARE(rate.http->requests.size(), 2);
    }
    void accountGates()
    {
        for (int i = 3; i < 7; ++i) {
            Harness guest({}, false);
            QSignalSpy done(&guest.service, &MusicQueryService::completed);
            QVERIFY(guest.service.request(guest.session.snapshot().token, queries()[i]));
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(std::get<MusicFailure>(result(done)).error,
                     MusicError::AuthenticationRequired);
            QVERIFY(guest.http->requests.isEmpty());
            Harness noCookie({}, true, false);
            QSignalSpy nc(&noCookie.service, &MusicQueryService::completed);
            QVERIFY(noCookie.service.request(noCookie.session.snapshot().token, queries()[i]));
            QTRY_COMPARE(nc.size(), 1);
            QVERIFY(std::holds_alternative<MusicFailure>(result(nc)));
            QVERIFY(noCookie.http->requests.isEmpty());
            Harness auth({});
            auto q = std::get<RecentEntitiesQuery>(queries()[i]);
            q.accountId = "102";
            QSignalSpy mismatch(&auth.service, &MusicQueryService::completed);
            QVERIFY(auth.service.request(auth.session.snapshot().token, q));
            QTRY_COMPARE(mismatch.size(), 1);
            // Same current token, caller-supplied foreign account: InvalidRequest.
            // StaleContext remains reserved for changed/obsolete session tokens.
            QCOMPARE(std::get<MusicFailure>(result(mismatch)).error, MusicError::InvalidRequest);
            QCOMPARE(auth.http->requests.size(), 1);
        }
    }
    void rejectionNoFallback()
    {
        Harness h({Plan{R"({"code":-462})"}}, false);
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token, queries()[1]));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::ProviderRejected);
        QCOMPARE(std::get<MusicFailure>(result(done)).providerCode, std::optional<int>(-462));
        QTest::qWait(15);
        QCOMPARE(h.http->requests.size(), 1);
    }
    void facadeContext()
    {
        BackendFacade f;
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        const SessionToken t{QUuid::createUuid(), 1};
        e.session = {t, SessionPhase::Guest};
        f.deliver(e);
        QSignalSpy req(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
            report(&f, &BackendFacade::listeningReportRequested);
        QVERIFY(!f.recentPlaylists());
        QVERIFY(!f.recentAlbums());
        QVERIFY(!f.recentPodcasts());
        QVERIFY(!f.recentVoices());
        QVERIFY(f.podcastDetails({"4"}));
        QVERIFY(f.podcastPrograms({{"4"}, {2, 0}}));
        QVERIFY(f.programDetails({"5"}));
        QVERIFY(!f.programDetails({"0"}));
        e.session = {t, SessionPhase::Authenticated, "101"};
        f.deliver(e);
        QVERIFY(f.recentPlaylists(2));
        QVERIFY(f.recentAlbums(2));
        QVERIFY(f.recentPodcasts(2));
        QVERIFY(f.recentVoices(2));
        for (int i = 3; i < 7; ++i) {
            const auto c = qvariant_cast<MusicCommand>(req[i][0]);
            QCOMPARE(musicQueryAccount(c.query), std::optional<QString>("101"));
            QCOMPARE(c.context, t);
        }
        QVERIFY(!f.recentVoices(0));
        const auto old = qvariant_cast<MusicCommand>(req.last()[0]);
        for (int i = 0; i < 9; ++i)
            QVERIFY(f.podcastDetails({"4"}));
        QVERIFY(!f.programDetails({"5"}));
        QVERIFY(f.signOut());
        QTRY_COMPARE(done.size(), 16);
        f.deliverMusic({old.id, old.generation, t,
                        CloudRecentVoices{std::get<RecentEntitiesQuery>(old.query)}});
        QCOMPARE(done.size(), 16);
        QVERIFY(play.isEmpty());
        QVERIFY(report.isEmpty());
    }
};
QTEST_GUILESS_MAIN(PodcastReadsTest)
#include "podcast_reads_test.moc"
