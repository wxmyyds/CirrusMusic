#include "application/playback/listening_session.h"
#include "domain/playback/listening_state_types.h"
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

// 心动模式 in the queue (docs/tasks/heartbeat-mode.md §7.3–7.5).
using namespace cirrus::application;
using namespace cirrus::domain;
namespace
{
const PlaylistId liked{"70"};
const PlayOrigin own{PlayOriginKind::Playlist, "70", "我喜欢的音乐"};
const PlayOrigin mixed{PlayOriginKind::Heartbeat, "70", "我喜欢的音乐·心动推荐"};
// Songs alternating with recommendations, as the service mixes them: "1" own, "r1" mixed…
QList<QueueEntry> mix(const QStringList& songs)
{
    QList<QueueEntry> entries;
    for (const auto& song : songs) {
        const bool recommended = song.startsWith(u'9');
        entries.append({{},
                        {song},
                        recommended ? mixed : own,
                        {},
                        recommended ? QStringLiteral("Alg_AI_One_1:2_mix") : QString()});
    }
    return entries;
}
QStringList songs(const PlaybackQueue& q)
{
    QStringList out;
    const auto entries = q.snapshot().entries;
    for (const auto id : q.playOrder())
        for (const auto& e : entries)
            if (e.id == id)
                out << e.song.value;
    return out;
}
QStringList songs(const QList<QueueEntry>& entries)
{
    QStringList out;
    for (const auto& e : entries)
        out << e.song.value;
    return out;
}
struct Harness {
    BackendFacade facade;
    ListeningSession player{facade};
    QList<PlaybackCommand> commands;
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
        facade.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        facade.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest, {}};
        facade.deliver(e);
    }
    int plays() const
    {
        int n = 0;
        for (const auto& c : commands)
            n += c.action == PlaybackAction::Play;
        return n;
    }
};
} // namespace
class HeartbeatQueueTest : public QObject
{
    Q_OBJECT
  private slots:
    void entersKeepingTheActiveSong()
    {
        PlaybackQueue q([](quint32) { return 0; });
        QVERIFY(q.replace({{"1"}, {"2"}, {"3"}, {"4"}}, {own, own, own, own}));
        QVERIFY(q.setModes(QueueOrder::Shuffle, QueueRepeat::Off));
        const auto before = q.snapshot();
        QVERIFY(q.select(before.entries[1].id)); // "2"
        QVERIFY(q.append({{"8"}}, true, {PlayOriginKind::Search, {}, {}}));
        QVERIFY(q.append({{"7"}}, true, {PlayOriginKind::Search, {}, {}}));
        QVERIFY(!q.enterHeartbeat(liked, mix({"3"}), true, "bad mark")); // not printable ASCII
        QVERIFY(q.enterHeartbeat(liked, mix({"3", "91", "2", "92", "1", "93"}), true,
                                 "Alg_AI_One_2_clicksong-RH"));
        QVERIFY(q.inHeartbeat());
        // The playing one first, its next ones, then the mix without it.
        QCOMPARE(songs(q), (QStringList{"2", "8", "7", "3", "91", "92", "1", "93"}));
        const auto s = q.snapshot();
        QCOMPARE(s.active, before.entries[1].id); // the same entry: it plays on
        QCOMPARE(s.order, QueueOrder::Sequential);
        QCOMPARE(s.repeat, QueueRepeat::All);
        QCOMPARE(s.entries[4].alg, "Alg_AI_One_1:2_mix");
        QCOMPARE(s.entries[4].origin, mixed);
        QVERIFY(s.heartbeat);
        QCOMPARE(s.heartbeat->playlist.value, liked.value);
        QCOMPARE(songs(s.heartbeat->entries), (QStringList{"1", "2", "8", "7", "3", "4"}));
        QCOMPARE(s.heartbeat->active, before.entries[1].id);
        QCOMPARE(s.entries[0].alg, "Alg_AI_One_2_clicksong-RH"); // in the mix only
        QVERIFY(s.heartbeat->entries[1].alg.isEmpty());
        QCOMPARE(s.heartbeat->order, QueueOrder::Shuffle);
        QCOMPARE(s.heartbeat->repeat, QueueRepeat::Off);
        QVERIFY(validSavedQueue(s));
        // Round and round, in its own order.
        for (int i = 0; i < 7; ++i)
            QVERIFY(q.next(true));
        QCOMPARE(q.current()->song.value, "93");
        QVERIFY(q.next(true));
        QCOMPARE(q.current()->song.value, "2");
        // Another playNext goes after the kept ones.
        QVERIFY(q.select(s.entries[0].id));
        QVERIFY(q.append({{"6"}}, true));
        QCOMPARE(songs(q).mid(0, 4), (QStringList{"2", "6", "8", "7"}));
    }
    void entersFromTheStart()
    {
        PlaybackQueue q;
        QVERIFY(q.replace({{"1"}, {"2"}}, {own, own}));
        QVERIFY(q.select(q.snapshot().entries[0].id));
        QVERIFY(q.enterHeartbeat(liked, mix({"2", "91", "1"}), false));
        QCOMPARE(songs(q), (QStringList{"2", "91", "1"}));
        QVERIFY(q.snapshot().active.isNull());
        QVERIFY(!q.enterHeartbeat(liked, {}, false));
        QVERIFY(!q.enterHeartbeat(PlaylistId{"0"}, mix({"1"}), false));
        QVERIFY(!q.enterHeartbeat(liked, mix({"01"}), false));
        auto bad = mix({"91"});
        bad.first().alg = "has space";
        QVERIFY(!q.enterHeartbeat(liked, bad, false));
        PlaybackQueue empty;
        QVERIFY(!empty.enterHeartbeat(liked, mix({"1"}), true)); // nothing active to keep
    }
    void againKeepsTheFirstQueueBefore()
    {
        PlaybackQueue q;
        QVERIFY(q.replace({{"1"}, {"2"}}, {own, own}));
        QVERIFY(q.select(q.snapshot().entries[1].id));
        QVERIFY(q.enterHeartbeat(liked, mix({"1", "91"}), true));
        QVERIFY(q.enterHeartbeat(liked, mix({"2", "92", "1"}), false));
        QCOMPARE(songs(q), (QStringList{"2", "92", "1"}));
        QCOMPARE(songs(q.snapshot().heartbeat->entries), (QStringList{"1", "2"}));
    }
    void leavingBringsTheQueueBack()
    {
        PlaybackQueue q([](quint32) { return 0; });
        QVERIFY(q.replace({{"1"}, {"2"}, {"3"}}, {own, own, own}));
        const auto before = q.snapshot();
        QVERIFY(q.select(before.entries[0].id));
        QVERIFY(q.enterHeartbeat(liked, mix({"2", "91", "3"}), true));
        QVERIFY(q.next());
        QVERIFY(q.next());
        QCOMPARE(q.current()->song.value, "91"); // a recommended one plays
        QVERIFY(q.setModes(QueueOrder::Sequential, QueueRepeat::All));
        QVERIFY(!q.inHeartbeat());
        // Put after the one playing back then, and still active.
        QCOMPARE(songs(q), (QStringList{"1", "91", "2", "3"}));
        QCOMPARE(q.current()->song.value, "91");
        QCOMPARE(q.snapshot().repeat, QueueRepeat::All);
        QVERIFY(q.next());
        QCOMPARE(q.current()->song.value, "2");
        // Leaving on one of its own songs: that entry, in its place.
        QVERIFY(q.enterHeartbeat(liked, mix({"3", "92"}), true)); // "2" playing
        QVERIFY(q.setModes(QueueOrder::Sequential, QueueRepeat::Off));
        QCOMPARE(songs(q), (QStringList{"1", "91", "2", "3"}));
        QCOMPARE(q.current()->song.value, "2");
        QVERIFY(!q.snapshot().heartbeat);
        // Shuffle on leaving: the playing one first.
        QVERIFY(q.enterHeartbeat(liked, mix({"3", "92"}), true));
        QVERIFY(q.setModes(QueueOrder::Shuffle, QueueRepeat::All));
        QCOMPARE(q.playOrder().first(), q.current()->id);
        QCOMPARE(q.snapshot().entries.size(), 4);
    }
    void addedSongsAreInBoth()
    {
        PlaybackQueue q;
        QVERIFY(q.replace({{"1"}, {"2"}, {"3"}}, {own, own, own}));
        QVERIFY(q.select(q.snapshot().entries[1].id));
        QVERIFY(q.enterHeartbeat(liked, mix({"1", "91", "3"}), true));
        QVERIFY(q.append({{"5"}}, true));
        QVERIFY(q.append({{"6"}}, true));
        QVERIFY(q.append({{"7"}}));
        QCOMPARE(songs(q), (QStringList{"2", "5", "6", "1", "91", "3", "7"}));
        QCOMPARE(songs(q.snapshot().heartbeat->entries),
                 (QStringList{"1", "2", "5", "6", "3", "7"}));
        QVERIFY(q.next());
        QCOMPARE(q.current()->song.value, "5");
        QVERIFY(q.setModes(QueueOrder::Sequential, QueueRepeat::All));
        QCOMPARE(q.current()->song.value, "5"); // the same entry in the queue before
        QCOMPARE(songs(q), (QStringList{"1", "2", "5", "6", "3", "7"}));
        // Removing in the mode leaves the queue before alone.
        QVERIFY(q.enterHeartbeat(liked, mix({"1", "91"}), true));
        QVERIFY(q.remove(q.snapshot().entries.last().id));
        QCOMPARE(q.snapshot().heartbeat->entries.size(), 6);
    }
    void replacingOrClearingEndsIt()
    {
        PlaybackQueue q;
        QVERIFY(q.replace({{"1"}, {"2"}}, {own, own}));
        QVERIFY(q.setModes(QueueOrder::Shuffle, QueueRepeat::One));
        QVERIFY(q.select(q.snapshot().entries[0].id));
        QVERIFY(q.enterHeartbeat(liked, mix({"2", "91"}), true));
        QVERIFY(q.replace({{"5"}, {"6"}}));
        QVERIFY(!q.inHeartbeat());
        QCOMPARE(q.snapshot().order, QueueOrder::Shuffle); // the modes from before it
        QCOMPARE(q.snapshot().repeat, QueueRepeat::One);
        QVERIFY(q.select(q.snapshot().entries[0].id));
        QVERIFY(q.enterHeartbeat(liked, mix({"91"}), true));
        q.clear();
        QVERIFY(!q.inHeartbeat());
        QCOMPARE(q.snapshot().order, QueueOrder::Shuffle);
    }
    void restoredAndValidated()
    {
        PlaybackQueue q;
        QVERIFY(q.replace({{"1"}, {"2"}}, {own, own}));
        QVERIFY(q.select(q.snapshot().entries[0].id));
        QVERIFY(q.enterHeartbeat(liked, mix({"2", "91"}), true));
        const auto saved = q.snapshot();
        PlaybackQueue back;
        QVERIFY(back.restore(saved));
        QCOMPARE(songs(back), (QStringList{"1", "2", "91"}));
        QVERIFY(back.setModes(QueueOrder::Sequential, QueueRepeat::Off));
        QCOMPARE(songs(back), (QStringList{"1", "2"}));
        auto bad = saved;
        bad.order = QueueOrder::Shuffle; // a mix plays in its own order
        QVERIFY(!validSavedQueue(bad));
        bad = saved;
        bad.heartbeat->playlist = PlaylistId{};
        QVERIFY(!validSavedQueue(bad));
        bad = saved;
        bad.heartbeat->active = QUuid::createUuid();
        QVERIFY(!validSavedQueue(bad));
        bad = saved;
        bad.entries[2].alg = QString(257, u'a');
        QVERIFY(!validSavedQueue(bad));
        bad = saved;
        bad.heartbeat->entries.append(bad.heartbeat->entries.first()); // same id twice
        QVERIFY(!validSavedQueue(bad));
        QVERIFY(!validPlayOrigin({PlayOriginKind::Heartbeat, {}, {}})); // needs the playlist
        QVERIFY(validPlayOrigin(mixed));
    }
    void sessionKeepsPlayingOrStartsTheMix()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}, {"2"}}, 0, AudioQuality::Standard, {own, own}));
        QTRY_COMPARE(h.plays(), 1);
        QSignalSpy changed(&h.player, &ListeningSession::queueChanged);
        QVERIFY(h.player.enterHeartbeat(liked, mix({"2", "91"}), true));
        QCOMPARE(changed.size(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(h.plays(), 1); // undisturbed
        QCOMPARE(songs(h.player.queue().entries), (QStringList{"1", "2", "91"}));
        QVERIFY(h.player.queue().heartbeat);
        QVERIFY(h.player.enterHeartbeat(liked, mix({"92", "2"}), false));
        QTRY_COMPARE(h.plays(), 2);
        QCOMPARE(h.commands.last().query.song.value, "92");
        QVERIFY(h.player.setModes(QueueOrder::Sequential, QueueRepeat::All));
        QVERIFY(!h.player.queue().heartbeat);
        QCOMPARE(songs(h.player.queue().entries), (QStringList{"1", "92", "2"}));
        QVERIFY(!h.player.enterHeartbeat(liked, {}, true));
    }
};
QTEST_GUILESS_MAIN(HeartbeatQueueTest)
#include "heartbeat_queue_test.moc"
