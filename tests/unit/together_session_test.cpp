#include "application/playback/listening_session.h"
#include "application/playback/together_session.h"
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace cirrus::application;
using namespace cirrus::domain;
namespace
{
const QString kMe = QStringLiteral("100");
const QString kFriend = QStringLiteral("200");
const TogetherRoomId kRoom{QStringLiteral("synthetic-room")};

struct Harness {
    QList<PlaybackCommand> commands;
    QList<TogetherCommand> sent; // not answered yet
    QList<TogetherCommand> all;
    qint64 clock = 1'000'000;
    SessionSnapshot session{{QUuid::createUuid(), 1}, SessionPhase::Authenticated, kMe};
    BackendFacade facade;
    ListeningSession player{facade};
    TogetherSession together{facade, player};
    Harness()
    {
        QObject::connect(&facade, &BackendFacade::playbackRequested, &facade,
                         [this](PlaybackCommand c) {
                             commands.append(c);
                             QTimer::singleShot(0, &facade, [this, c] {
                                 BackendEvent e;
                                 e.kind = BackendEvent::Kind::Acknowledged;
                                 e.commandId = c.id;
                                 e.accepted = true;
                                 facade.deliver(e);
                             });
                         });
        QObject::connect(&facade, &BackendFacade::togetherRequested, &facade,
                         [this](TogetherCommand c) {
                             sent.append(c);
                             all.append(c);
                         });
        facade.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        facade.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = session;
        facade.deliver(e);
        together.setClock([this] { return clock; });
        TogetherSession::Timing t;
        t.pollMs = 10;
        t.statusMs = 1000000;
        t.heartbeatMs = 1000000;
        t.lostMs = 60000;
        t.reportMs = 5;
        t.replaceMs = 5;
        t.leaveMs = 200;
        together.setTiming(t);
    }
    PlaybackCommand latestPlay() const
    {
        for (auto i = commands.crbegin(); i != commands.crend(); ++i)
            if (i->action == PlaybackAction::Play)
                return *i;
        return {};
    }
    // Publishes the player's state for the load playing now.
    void publish(PlaybackState state, qint64 position = 0, quint64 seeks = 0,
                 SourceProvider provider = SourceProvider::Netease)
    {
        const auto c = latestPlay();
        PlaybackSnapshot s;
        s.token = c.token;
        s.song = c.query.song;
        s.state = state;
        s.intent = c.intent;
        s.positionMs = position;
        s.durationMs = 300000;
        s.seekable = true;
        s.seekRevision = seeks;
        s.provider = provider;
        facade.deliverPlayback({c.generation, s});
    }
    int count(TogetherKind kind) const
    {
        return int(std::count_if(all.cbegin(), all.cend(), [kind](const TogetherCommand& c) {
            return togetherKind(c.request.action) == kind;
        }));
    }
    bool has(TogetherKind kind) const
    {
        return std::any_of(sent.cbegin(), sent.cend(), [kind](const TogetherCommand& c) {
            return togetherKind(c.request.action) == kind;
        });
    }
    // The oldest unanswered request of `kind` (waits for it).
    TogetherCommand take(TogetherKind kind)
    {
        if (!QTest::qWaitFor([&] { return has(kind); }, 2000)) {
            qWarning("no %s request", qPrintable(logName(kind)));
            TogetherCommand none;
            none.request.action = TogetherStatus{};
            return none;
        }
        const auto i = std::find_if(sent.begin(), sent.end(), [kind](const TogetherCommand& c) {
            return togetherKind(c.request.action) == kind;
        });
        auto c = *i;
        sent.erase(i);
        return c;
    }
    void reply(const TogetherCommand& c, TogetherResult r)
    {
        r.eventId = c.request.eventId;
        r.kind = togetherKind(c.request.action);
        r.roomGeneration = c.request.roomGeneration;
        facade.deliverTogether({c.id, c.generation, c.context, r});
    }
    static TogetherResult ok()
    {
        TogetherResult r;
        r.outcome = TogetherOutcome::Acknowledged;
        r.providerCode = 200;
        return r;
    }
    static TogetherRoom room(QList<QString> members = {kMe})
    {
        TogetherRoom r{kRoom};
        r.creatorId = kMe;
        for (const auto& m : members)
            r.members.append(TogetherMember{m, QStringLiteral("synthetic"), {}});
        return r;
    }
    static TogetherResult list(QList<SongId> songs, QList<TogetherVersion> versions,
                               std::optional<TogetherReportedPlay> play = {})
    {
        auto r = ok();
        TogetherPlaylist p;
        p.display = std::move(songs);
        p.displayChanged = true;
        p.replace = true;
        p.mode = TogetherPlayMode::OrderLoop;
        p.versions = std::move(versions);
        r.playlist = p;
        r.reportedPlay = std::move(play);
        return r;
    }
    static TogetherReportedPlay command(TogetherPlayAction action, SongId target,
                                        TogetherPlayStatus status, qint64 progress,
                                        qint64 serverSeq, QString user = kFriend)
    {
        TogetherReportedPlay p;
        p.action = action;
        p.target = target;
        p.former = target;
        p.status = status;
        p.progressMs = progress;
        p.serverSeq = serverSeq;
        p.clientSeq = 1;
        p.userId = std::move(user);
        p.trigger = TogetherTrigger::Manual;
        return p;
    }
    // Creates a room from the queue 1, 2, 3 playing 2; ends active.
    void createActive()
    {
        QVERIFY(player.replaceQueue({{"1"}, {"2"}, {"3"}}, 1));
        publish(PlaybackState::Playing, 5000);
        QVERIFY(together.canCreate());
        QVERIFY(together.create());
        auto r = ok();
        r.inRoom = false;
        reply(take(TogetherKind::Status), r);
        r = ok();
        r.resultType = QStringLiteral("NEW_ROOM");
        r.room = room();
        reply(take(TogetherKind::Create), r);
        const auto q = take(TogetherKind::ReplaceQueue);
        const auto& a = std::get<TogetherReplaceQueue>(q.request.action);
        QCOMPARE(a.list.display, (QList<SongId>{{"1"}, {"2"}, {"3"}}));
        QCOMPARE(a.list.anchor.value, "2");
        QCOMPARE(a.list.anchorPosition, 1);
        QCOMPARE(a.versions, (QList<TogetherVersion>{{kMe, 0}}));
        publish(PlaybackState::Playing, 5000);
        reply(q, ok());
        const auto play = take(TogetherKind::Play);
        const auto& p = std::get<TogetherPlay>(play.request.action);
        QCOMPARE(p.action, TogetherPlayAction::Play);
        QCOMPARE(p.target.value, "2");
        QCOMPARE(p.progressMs, 5000);
        reply(play, ok());
        QCOMPARE(together.state(), TogetherSession::State::Active);
        QVERIFY(together.isCreator());
    }
    // Answers the next room read with the room's list 1, 2, 3 and `play`.
    void read(std::optional<TogetherReportedPlay> play = {},
              QList<TogetherVersion> versions = {{kMe, 0}})
    {
        reply(take(TogetherKind::Queue), list({{"1"}, {"2"}, {"3"}}, versions, play));
    }
};
} // namespace

class TogetherSessionTest : public QObject
{
    Q_OBJECT
  private slots:
    void bilibiliAndProgramsCannotStartRooms()
    {
        Harness h;
        h.facade.setAllowBiliSource(true);
        QVERIFY(h.player.replaceQueue({{"1"}}));
        h.publish(PlaybackState::Playing, 1000, 0, SourceProvider::Bilibili);
        QVERIFY(!h.together.canCreate());
        QVERIFY(!h.together.create());
        QVERIFY(!h.together.join({kRoom, kFriend}));
        QCOMPARE(h.all.size(), 0);
        QVERIFY(h.player.replaceQueue({{"1"}}, 0, AudioQuality::Standard, {}, {{"5"}}));
        QVERIFY(!h.together.create());
    }
    void roomsRestrictSourcesAndReleaseTheRestriction()
    {
        Harness h;
        h.facade.setAllowBiliSource(true);
        h.createActive();
        QVERIFY(!h.latestPlay().query.allowBiliSource);
        const auto roomLoad = h.latestPlay().id;
        QVERIFY(h.player.next());
        QVERIFY(!h.latestPlay().query.allowBiliSource);
        h.publish(PlaybackState::Playing);
        int roomPreloads = 0;
        for (const auto& c : h.commands)
            if (c.id > roomLoad && c.action == PlaybackAction::Preload &&
                !c.query.song.value.isEmpty()) {
                ++roomPreloads;
                QVERIFY(!c.query.allowBiliSource);
            }
        QVERIFY(roomPreloads > 0);
        const auto before = h.count(TogetherKind::Play);
        h.publish(PlaybackState::Playing, 1000, 0, SourceProvider::Bilibili);
        QCOMPARE(h.together.state(), TogetherSession::State::Idle);
        QTest::qWait(30);
        QCOMPARE(h.count(TogetherKind::Play), before);
        QVERIFY(h.facade.allowBiliSource());
        QVERIFY(h.player.replaceQueue({{"9"}}));
        QVERIFY(h.latestPlay().query.allowBiliSource);
    }
    void roomEntryRetiresAllowedLoadsAfterSettingIsDisabled()
    {
        for (bool resume : {false, true}) {
            Harness h;
            h.facade.setAllowBiliSource(true);
            QVERIFY(h.player.replaceQueue({{"1"}, {"2"}, {"3"}}, 1));
            h.publish(PlaybackState::Playing, 5000);
            const auto old = h.latestPlay();
            QVERIFY(old.query.allowBiliSource);
            const auto preload = std::find_if(h.commands.crbegin(), h.commands.crend(),
                [](const auto& c) {
                    return c.action == PlaybackAction::Preload && !c.query.song.value.isEmpty();
                });
            QVERIFY(preload != h.commands.crend());
            const auto oldPreload = *preload;
            QVERIFY(oldPreload.query.allowBiliSource);
            h.facade.setAllowBiliSource(false);
            if (resume) {
                QVERIFY(h.together.resume({kRoom, kMe, false}));
                auto r = Harness::ok();
                r.inRoom = true;
                r.room = Harness::room();
                h.reply(h.take(TogetherKind::Status), r);
            } else {
                QVERIFY(h.together.join({kRoom, kFriend}));
                auto r = Harness::ok();
                r.joinable = true;
                h.reply(h.take(TogetherKind::Check), r);
                r = Harness::ok();
                r.room = Harness::room();
                h.reply(h.take(TogetherKind::Join), r);
            }
            h.reply(h.take(TogetherKind::Queue),
                    Harness::list({{"1"}, {"2"}, {"3"}}, {{kMe, 0}},
                                  Harness::command(TogetherPlayAction::Play, {"2"},
                                                   TogetherPlayStatus::Play, 5000, h.clock)));
            QCOMPARE(h.together.state(), TogetherSession::State::Active);
            QVERIFY(h.latestPlay().id != old.id);
            QVERIFY(!h.latestPlay().query.allowBiliSource);
            QVERIFY(!h.facade.allowBiliSource());
            const auto current = h.player.playback().token;
            PlaybackSnapshot late;
            late.token = oldPreload.next;
            late.song = oldPreload.query.song;
            late.provider = SourceProvider::Bilibili;
            late.state = PlaybackState::Playing;
            h.facade.deliverPlayback({oldPreload.generation, late});
            QCOMPARE(h.player.playback().token, current);
            QCOMPARE(h.together.state(), TogetherSession::State::Active);
        }
    }
    void failedOrCanceledRoomReloadNeverPublishesTheQueue()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}}));
        QVERIFY(h.together.create());
        auto r = Harness::ok();
        r.inRoom = false;
        h.reply(h.take(TogetherKind::Status), r);
        const auto create = h.take(TogetherKind::Create);
        QCoreApplication::processEvents();
        for (int i = 0; i < 32; ++i)
            QVERIFY(h.facade.setPlaybackVolume(0));
        r = Harness::ok();
        r.room = Harness::room();
        h.reply(create, r);
        QCOMPARE(h.together.state(), TogetherSession::State::Idle);
        QCOMPARE(h.count(TogetherKind::ReplaceQueue), 0);
        QCOMPARE(h.count(TogetherKind::Play), 0);

        Harness canceled;
        QVERIFY(canceled.player.replaceQueue({{"1"}}));
        QVERIFY(canceled.together.create());
        r = Harness::ok();
        r.inRoom = false;
        canceled.reply(canceled.take(TogetherKind::Status), r);
        const auto pending = canceled.take(TogetherKind::Create);
        connect(&canceled.player, &ListeningSession::playbackChanged, this,
                [&] { canceled.together.leave(); });
        r = Harness::ok();
        r.room = Harness::room();
        canceled.reply(pending, r);
        QCOMPARE(canceled.together.state(), TogetherSession::State::Leaving);
        QCOMPARE(canceled.count(TogetherKind::ReplaceQueue), 0);
        QCOMPARE(canceled.count(TogetherKind::Play), 0);
    }
    void bilibiliSelectionDuringStartupEndsTheRoom()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}}));
        QVERIFY(h.together.create());
        const auto c = h.latestPlay();
        h.facade.deliverPlaybackSource(
            {c.token, c.query.song, SourceProvider::Bilibili,
             BiliStreamMeta{"BV1testpage", "456", "30280", {}}});
        QCOMPARE(h.together.state(), TogetherSession::State::Idle);
        QCOMPARE(h.count(TogetherKind::ReplaceQueue), 0);
        QCOMPARE(h.count(TogetherKind::Play), 0);
    }
    void parsesShareLinks()
    {
        const auto i = TogetherSession::parseInvite(QStringLiteral(
            "https://st.music.163.com/listen-together/share/index.html?roomId=ab_c-1&inviterId=42"
            "&songId=7"));
        QVERIFY(i);
        QCOMPARE(i->room.value, "ab_c-1");
        QCOMPARE(i->inviterId, "42");
        QVERIFY(!TogetherSession::parseInvite(QStringLiteral("https://x/?roomId=ab")));
        QVERIFY(!TogetherSession::parseInvite(QStringLiteral("file:///?roomId=ab&inviterId=42")));
        QVERIFY(
            !TogetherSession::parseInvite(QStringLiteral("https://x/?roomId=a%20b&inviterId=4")));
        // As the app copies it: words around the link, with or without a space.
        for (const auto& text : {
                 QStringLiteral("我的耳机分你一半，和我一起听歌吧～快点开看看 "
                                "https://st.music.163.com/listen-together/share/"
                                "index.html?roomId=7e39_17&inviterId=42&songId=7 (@网易云音乐)\n"),
                 QStringLiteral("快点开看看https://st.music.163.com/listen-together/share/"
                                "index.html?roomId=7e39_17&inviterId=42（@网易云音乐）"),
                 QStringLiteral("https://x/a 和 https://st.music.163.com/listen-together/share/"
                                "index.html?roomId=7e39_17&inviterId=42."),
             }) {
            const auto copied = TogetherSession::parseInvite(text);
            QVERIFY2(copied, qPrintable(text));
            QCOMPARE(copied->room.value, "7e39_17");
            QCOMPARE(copied->inviterId, "42");
        }
        QVERIFY(!TogetherSession::parseInvite(QStringLiteral("roomId=ab&inviterId=42")));
    }
    void createsFromTheQueue()
    {
        Harness h;
        h.createActive();
        const auto link = h.together.shareLink();
        QVERIFY(link.contains(u"roomId=synthetic-room"));
        QVERIFY(link.contains(u"inviterId=100"));
        QVERIFY(link.contains(u"songId=2"));
    }
    void cannotCreateWithoutQueueOrAccount()
    {
        Harness h;
        QVERIFY(!h.together.canCreate());
        QVERIFY(!h.together.create());
        QVERIFY(h.player.replaceQueue({{"1"}}));
        BackendEvent e;
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 2}, SessionPhase::Guest, {}};
        h.facade.deliver(e);
        QVERIFY(!h.together.canCreate());
    }
    void appliesAnotherMembersCommand()
    {
        Harness h;
        h.createActive();
        const int plays = h.count(TogetherKind::Play);
        h.read(Harness::command(TogetherPlayAction::Goto, {"3"}, TogetherPlayStatus::Play, 0,
                                h.clock));
        QTRY_COMPARE(h.latestPlay().query.song.value, "3");
        h.publish(PlaybackState::Playing, 0);
        QTest::qWait(50);
        QCOMPARE(h.count(TogetherKind::Play), plays); // not reported back
        // The same command again is not applied twice; a pause from them is.
        h.read(Harness::command(TogetherPlayAction::Goto, {"3"}, TogetherPlayStatus::Play, 0,
                                h.clock));
        h.read(Harness::command(TogetherPlayAction::Pause, {"3"}, TogetherPlayStatus::Pause, 0,
                                h.clock + 1));
        QTRY_COMPARE(h.player.playback().intent, TransportIntent::Pause);
        QTest::qWait(50);
        QCOMPARE(h.count(TogetherKind::Play), plays);
    }
    void correctsThePosition()
    {
        Harness h;
        h.createActive();
        const int seeks = int(std::count_if(h.commands.cbegin(), h.commands.cend(), [](auto& c) {
            return c.action == PlaybackAction::Seek;
        }));
        h.read(Harness::command(TogetherPlayAction::Progress, {"2"}, TogetherPlayStatus::Play,
                                90000, h.clock));
        QTRY_VERIFY(std::count_if(h.commands.cbegin(), h.commands.cend(), [](auto& c) {
                        return c.action == PlaybackAction::Seek;
                    }) == seeks + 1);
        const int plays = h.count(TogetherKind::Play);
        h.publish(PlaybackState::Playing, 90000, 1); // the seek shows
        QTest::qWait(50);
        QCOMPARE(h.count(TogetherKind::Play), plays);
        // A seek of the user's own afterwards is reported.
        QVERIFY(h.player.seek(120000));
        h.publish(PlaybackState::Playing, 120000, 2);
        const auto c = h.take(TogetherKind::Play);
        QCOMPARE(std::get<TogetherPlay>(c.request.action).action, TogetherPlayAction::Progress);
    }
    void ignoresOwnCommands()
    {
        Harness h;
        h.createActive();
        h.read(Harness::command(TogetherPlayAction::Goto, {"3"}, TogetherPlayStatus::Play, 0,
                                h.clock, kMe));
        QTest::qWait(50);
        QCOMPARE(h.latestPlay().query.song.value, "2");
    }
    void reportsLocalChanges()
    {
        Harness h;
        h.createActive();
        QVERIFY(h.player.pause());
        auto c = h.take(TogetherKind::Play);
        auto p = std::get<TogetherPlay>(c.request.action);
        QCOMPARE(p.action, TogetherPlayAction::Pause);
        QCOMPARE(p.status, TogetherPlayStatus::Pause);
        QCOMPARE(p.target.value, "2");
        QVERIFY(h.player.resume());
        QVERIFY(h.player.next());
        c = h.take(TogetherKind::Play);
        p = std::get<TogetherPlay>(c.request.action);
        QCOMPARE(p.action, TogetherPlayAction::Goto); // the last of the burst
        QCOMPARE(p.target.value, "3");
        QCOMPARE(p.former.value, "2");
        QVERIFY(p.clientSeq > 1);
        // A song that ended: NEXT, AUTO. Repeat all keeps the play order: no REPLACE.
        QVERIFY(h.player.setModes(QueueOrder::Sequential, QueueRepeat::All));
        h.publish(PlaybackState::Playing, 1000);
        h.publish(PlaybackState::Ended, 300000);
        c = h.take(TogetherKind::Play);
        p = std::get<TogetherPlay>(c.request.action);
        QCOMPARE(p.action, TogetherPlayAction::Next);
        QCOMPARE(p.trigger, TogetherTrigger::Auto);
        QCOMPARE(p.target.value, "1");
        QCOMPARE(h.count(TogetherKind::ReplaceQueue), 1);
    }
    void sendsAChangedQueue()
    {
        Harness h;
        h.createActive();
        QVERIFY(h.player.replaceQueue({{"7"}, {"8"}}));
        const auto q = h.take(TogetherKind::ReplaceQueue);
        const auto& a = std::get<TogetherReplaceQueue>(q.request.action);
        QCOMPARE(a.list.display, (QList<SongId>{{"7"}, {"8"}}));
        QCOMPARE(a.versions, (QList<TogetherVersion>{{kMe, 1}}));
        // A read from before the service took it does not undo it.
        h.read({}, {{kMe, 0}});
        QTest::qWait(30);
        QCOMPARE(h.player.queue().entries.size(), 2);
        QVERIFY(!h.has(TogetherKind::Play)); // the song change waits for the list
        h.reply(q, Harness::ok());
        const auto c = h.take(TogetherKind::Play);
        QCOMPARE(std::get<TogetherPlay>(c.request.action).target.value, "7");
    }
    void joinsAndPlaysTheRoomsList()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}}));
        const auto invite = TogetherSession::parseInvite(QStringLiteral(
            "https://st.music.163.com/listen-together/share/index.html?roomId=synthetic-room"
            "&inviterId=200"));
        QVERIFY(invite);
        QVERIFY(h.together.join(*invite));
        auto r = Harness::ok();
        r.joinable = true;
        h.reply(h.take(TogetherKind::Check), r);
        const auto join = h.take(TogetherKind::Join);
        QCOMPARE(std::get<TogetherJoin>(join.request.action).inviterId, kFriend);
        r = Harness::ok();
        r.room = Harness::room({kFriend, kMe});
        r.room->creatorId = kFriend;
        h.reply(join, r);
        h.reply(h.take(TogetherKind::Queue),
                Harness::list({{"7"}, {"8"}, {"9"}}, {{kFriend, 3}},
                              Harness::command(TogetherPlayAction::Pause, {"8"},
                                               TogetherPlayStatus::Pause, 30000, h.clock)));
        h.take(TogetherKind::SyncNotice);
        QCOMPARE(h.together.state(), TogetherSession::State::Active);
        QVERIFY(!h.together.isCreator());
        QCOMPARE(h.together.members().size(), 2);
        QCOMPARE(h.player.queue().entries.size(), 3);
        QCOMPARE(h.latestPlay().query.song.value, "8");
        QCOMPARE(h.player.playback().intent, TransportIntent::Pause);
        h.publish(PlaybackState::Paused, 0);
        QTRY_VERIFY(std::any_of(h.commands.cbegin(), h.commands.cend(),
                                [](auto& c) { return c.action == PlaybackAction::Seek; }));
        h.publish(PlaybackState::Paused, 30000, 1);
        QTest::qWait(50);
        QCOMPARE(h.count(TogetherKind::Play), 0);
        QCOMPARE(h.count(TogetherKind::ReplaceQueue), 0);
    }
    void notJoinable()
    {
        Harness h;
        QSignalSpy ended(&h.together, &TogetherSession::ended);
        QVERIFY(h.together.join({kRoom, kFriend}));
        auto r = Harness::ok();
        r.joinable = false;
        h.reply(h.take(TogetherKind::Check), r);
        QCOMPARE(ended.size(), 1);
        QCOMPARE(ended[0][0].value<TogetherSession::Reason>(),
                 TogetherSession::Reason::NotJoinable);
        QCOMPARE(h.together.state(), TogetherSession::State::Idle);
        QVERIFY(!h.together.join({kRoom, kMe})); // not one's own room
    }
    void endsWhenTheRoomIsOver()
    {
        Harness h;
        h.createActive();
        QSignalSpy ended(&h.together, &TogetherSession::ended);
        TogetherResult r;
        r.outcome = TogetherOutcome::Rejected;
        r.error = TogetherError::ProviderRejected;
        r.providerCode = 488;
        h.reply(h.take(TogetherKind::Queue), r);
        QCOMPARE(ended.size(), 1);
        QCOMPARE(ended[0][0].value<TogetherSession::Reason>(), TogetherSession::Reason::RoomEnded);
        QCOMPARE(h.count(TogetherKind::End), 0);
        QTest::qWait(30);
        QCOMPARE(h.count(TogetherKind::Queue), 1); // the loop stopped
    }
    void leaves()
    {
        Harness h;
        h.createActive();
        QSignalSpy ended(&h.together, &TogetherSession::ended);
        h.together.leave();
        QCOMPARE(h.together.state(), TogetherSession::State::Leaving);
        const auto end = h.take(TogetherKind::End);
        QCOMPARE(std::get<TogetherEnd>(end.request.action).room, kRoom);
        h.reply(end, Harness::ok());
        QCOMPARE(ended.size(), 1);
        QCOMPARE(ended[0][0].value<TogetherSession::Reason>(), TogetherSession::Reason::Left);
        QCOMPARE(h.together.state(), TogetherSession::State::Idle);
        QVERIFY(h.together.shareLink().isEmpty());
    }
    void closeKeepsTheRoom()
    {
        Harness h;
        h.createActive();
        const auto saved = h.together.saved();
        QVERIFY(saved);
        QCOMPARE(saved->room, kRoom);
        QCOMPARE(saved->accountId, kMe);
        QVERIFY(!saved->joining);
        QSignalSpy changed(&h.together, &TogetherSession::changed);
        const int reads = h.count(TogetherKind::Queue);
        h.together.close();
        QVERIFY(h.player.pause()); // what the player does while closing is not reported
        QTest::qWait(50);
        QCOMPARE(h.count(TogetherKind::End), 0);
        QCOMPARE(h.count(TogetherKind::Play), 1);
        QVERIFY(h.count(TogetherKind::Queue) <= reads + 1);
        QCOMPARE(changed.size(), 0);
    }
    void resumesTheRoom()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}, {"2"}, {"3"}}, 0));
        QVERIFY(!h.together.resume({kRoom, kFriend, false})); // another account's
        QVERIFY(h.together.resume({kRoom, kMe, false}));
        QVERIFY(h.together.resuming());
        auto r = Harness::ok();
        r.inRoom = true;
        r.room = Harness::room({kMe, kFriend});
        h.reply(h.take(TogetherKind::Status), r);
        // This client's own last command, 30 s ago, playing: the room plays on from there.
        h.reply(
            h.take(TogetherKind::Queue),
            Harness::list({{"1"}, {"2"}, {"3"}}, {{kMe, 0}},
                          Harness::command(TogetherPlayAction::Goto, {"2"},
                                           TogetherPlayStatus::Play, 60000, h.clock - 30000, kMe)));
        QCOMPARE(h.together.state(), TogetherSession::State::Active);
        QVERIFY(h.together.isCreator());
        QCOMPARE(h.together.members().size(), 2);
        QCOMPARE(h.latestPlay().query.song.value, "2");
        QCOMPARE(h.player.playback().intent, TransportIntent::Play);
        h.publish(PlaybackState::Playing, 0);
        QTRY_VERIFY(std::any_of(h.commands.cbegin(), h.commands.cend(), [](auto& c) {
            return c.action == PlaybackAction::Seek && c.positionMs == 90000;
        }));
        QCOMPARE(h.count(TogetherKind::Check) + h.count(TogetherKind::Join) +
                     h.count(TogetherKind::SyncNotice),
                 0);
        h.publish(PlaybackState::Playing, 90000, 1);
        QTest::qWait(50);
        QCOMPARE(h.count(TogetherKind::Play), 0);
        QCOMPARE(h.count(TogetherKind::ReplaceQueue), 0);
    }
    void resumeEndsWithoutTheRoom()
    {
        Harness h;
        QSignalSpy ended(&h.together, &TogetherSession::ended);
        QVERIFY(h.together.resume({kRoom, kMe, true}));
        auto r = Harness::ok();
        r.inRoom = false;
        h.reply(h.take(TogetherKind::Status), r);
        QCOMPARE(ended.size(), 1);
        QCOMPARE(ended[0][0].value<TogetherSession::Reason>(), TogetherSession::Reason::RoomEnded);
        QCOMPARE(h.count(TogetherKind::End), 0);
        QVERIFY(!h.together.saved());
    }
    void losesTheConnection()
    {
        Harness h;
        h.createActive();
        QSignalSpy ended(&h.together, &TogetherSession::ended);
        TogetherResult r;
        r.outcome = TogetherOutcome::Unknown;
        r.error = TogetherError::Network;
        h.clock += 30000;
        h.reply(h.take(TogetherKind::Queue), r);
        QVERIFY(!h.together.connected());
        QCOMPARE(ended.size(), 0);
        h.clock += 30000;
        h.reply(h.take(TogetherKind::Queue), r);
        QCOMPARE(ended.size(), 1);
        QCOMPARE(ended[0][0].value<TogetherSession::Reason>(),
                 TogetherSession::Reason::ConnectionLost);
        h.take(TogetherKind::End); // best effort
    }
    void endsOnSignOut()
    {
        Harness h;
        h.createActive();
        QSignalSpy ended(&h.together, &TogetherSession::ended);
        BackendEvent e;
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 2}, SessionPhase::Guest, {}};
        h.facade.deliver(e);
        QCOMPARE(ended.size(), 1);
        QCOMPARE(ended[0][0].value<TogetherSession::Reason>(), TogetherSession::Reason::SignedOut);
    }
    void followsTheRoomsNewList()
    {
        Harness h;
        h.createActive();
        h.reply(h.take(TogetherKind::Queue),
                Harness::list({{"4"}, {"5"}}, {{kMe, 0}, {kFriend, 1}},
                              Harness::command(TogetherPlayAction::Goto, {"5"},
                                               TogetherPlayStatus::Play, 0, h.clock)));
        QCOMPARE(h.player.queue().entries.size(), 2);
        QCOMPARE(h.latestPlay().query.song.value, "5");
        h.publish(PlaybackState::Playing, 0);
        QTest::qWait(50);
        QCOMPARE(h.count(TogetherKind::ReplaceQueue), 1); // only the first
        QCOMPARE(h.count(TogetherKind::Play), 1);
    }
};

QTEST_MAIN(TogetherSessionTest)
#include "together_session_test.moc"
