#include "application/music/personal_fm_source.h"
#include "application/playback/listening_session.h"
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>
using namespace cirrus::application;
using namespace cirrus::domain;
namespace
{
struct Harness {
    BackendFacade f;
    PersonalFmSource source{f};
    ListeningSession player{f, source};
    QList<PlaybackCommand> commands;
    QList<MusicCommand> music;
    bool acknowledge = true;
    explicit Harness(bool account = true)
    {
        QObject::connect(&f, &BackendFacade::playbackRequested, &f, [this](PlaybackCommand c) {
            commands.append(c);
            if (acknowledge)
                QTimer::singleShot(0, &f, [this, c] { ack(c.id); });
        });
        QObject::connect(&f, &BackendFacade::musicRequested, &f,
                         [this](MusicCommand c) { music.append(c); });
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1},
                     account ? SessionPhase::Authenticated : SessionPhase::Guest,
                     account ? QString("1") : QString{}};
        f.deliver(e);
    }
    void ack(quint64 id)
    {
        BackendEvent e;
        e.kind = BackendEvent::Kind::Acknowledged;
        e.commandId = id;
        e.accepted = true;
        f.deliver(e);
    }
    void releaseMailbox()
    {
        acknowledge = true;
        for (const auto& c : commands)
            ack(c.id);
    }
    void fillMailbox()
    {
        QCoreApplication::processEvents();
        acknowledge = false;
        for (int i = 0; i < 32; ++i)
            QVERIFY(f.setPlaybackVolume(0));
    }
    int plays() const
    {
        return std::count_if(commands.cbegin(), commands.cend(),
                             [](auto c) { return c.action == PlaybackAction::Play; });
    }
    PlaybackCommand play() const
    {
        for (auto i = commands.crbegin(); i != commands.crend(); ++i)
            if (i->action == PlaybackAction::Play)
                return *i;
        return {};
    }
    MusicCommand fm() const
    {
        for (auto i = music.crbegin(); i != music.crend(); ++i)
            if (std::holds_alternative<PersonalFmQuery>(i->query))
                return *i;
        return {};
    }
    MusicCommand lyric()
    {
        const auto c = play();
        f.deliverPlaybackSource({c.token, c.query.song, SourceProvider::Netease, {}});
        for (auto i = music.crbegin(); i != music.crend(); ++i)
            if (std::holds_alternative<PlaybackLyricQuery>(i->query))
                return *i;
        return {};
    }
    int batches() const
    {
        return std::count_if(music.cbegin(), music.cend(), [](auto c) {
            return std::holds_alternative<PersonalFmQuery>(c.query);
        });
    }
    void deliver(const MusicCommand& c, MusicResult r)
    {
        f.deliverMusic({c.id, c.generation, c.context, std::move(r)});
    }
    void batch(std::initializer_list<int> ids)
    {
        PersonalFmBatch b;
        for (auto id : ids) {
            Song s;
            s.id = {QString::number(id)};
            b.songs.append(s);
        }
        deliver(fm(), b);
    }
    void publish(PlaybackState state, qint64 pos = 0)
    {
        const auto c = play();
        PlaybackSnapshot s;
        s.token = c.token;
        s.song = c.query.song;
        s.state = state;
        s.intent = c.intent;
        s.positionMs = pos;
        s.seekable = true;
        s.durationMs = 3000;
        if (state == PlaybackState::Failed)
            s.failure = PlaybackFailure{PlaybackError::Unavailable};
        f.deliverPlayback({c.generation, s});
    }
};
} // namespace
class FmListeningTest : public QObject
{
    Q_OBJECT
  private slots:
    void gates()
    {
        Harness guest(false);
        QVERIFY(!guest.player.startFm());
        QCOMPARE(guest.batches(), 0);
        QVERIFY(guest.commands.isEmpty());
        Harness h;
        ListeningSession queueOnly(h.f);
        QVERIFY(!queueOnly.startFm());
        Harness other;
        ListeningSession mismatched(h.f, other.source);
        QVERIFY(!mismatched.startFm());
        QCOMPARE(other.batches(), 0);
        QVERIFY(!h.player.startFm(static_cast<AudioQuality>(99)));
        QVERIFY(!h.player.startFm(AudioQuality::Standard, TransportIntent::Stop));
        QCOMPARE(h.player.mode(), ListeningMode::Queue);
        QVERIFY(h.commands.isEmpty());
    }
    void pausedStartNextAndLyrics()
    {
        Harness h;
        QVERIFY(h.player.startFm(AudioQuality::Standard, TransportIntent::Pause));
        QVERIFY(h.player.waitingForFm());
        QVERIFY(h.player.pause());
        QCOMPARE(h.plays(), 0);
        h.batch({1, 2, 3});
        QTRY_COMPARE(h.plays(), 1);
        QCOMPARE(h.play().intent, TransportIntent::Pause);
        QVERIFY(!h.player.waitingForFm());
        QVERIFY(h.player.queue().entries.isEmpty());
        const auto old = h.lyric();
        SongLyrics l;
        l.song = {"1"};
        l.original.standard.availability = LyricAvailability::Present;
        l.original.standard.timing = LyricTiming::Lines;
        l.original.standard.lines.append({0, std::nullopt, "synthetic", {}});
        h.deliver(old, l);
        h.publish(PlaybackState::Paused, 1200);
        QCOMPARE(h.player.cursors().original.standard.lines, (QList<qsizetype>{0}));
        QVERIFY(h.player.seek(100));
        QCOMPARE(h.commands.last().positionMs, 100);
        QVERIFY(h.player.next());
        QTRY_COMPARE(h.plays(), 2);
        QCOMPARE(h.play().query.song.value, "2");
        QCOMPARE(h.play().intent, TransportIntent::Pause);
        h.deliver(old, l);
        QVERIFY(!h.player.lyrics());
        QVERIFY(!h.player.append({{"9"}}));
        QVERIFY(!h.player.setModes(QueueOrder::Shuffle, QueueRepeat::All));
        QVERIFY(!h.player.select(QUuid::createUuid()));
    }
    void endOnceAndIgnoreOrdinaryRepeat()
    {
        Harness h;
        QVERIFY(h.player.setModes(QueueOrder::Sequential, QueueRepeat::One));
        QVERIFY(h.player.startFm());
        h.batch({1, 2, 3});
        QTRY_COMPARE(h.plays(), 1);
        const auto old = h.play();
        h.publish(PlaybackState::Ended);
        h.publish(PlaybackState::Ended);
        QTRY_COMPARE(h.plays(), 2);
        QCOMPARE(h.play().query.song.value, "2");
        PlaybackSnapshot late;
        late.token = old.token;
        late.state = PlaybackState::Ended;
        h.f.deliverPlayback({old.generation, late});
        QCoreApplication::processEvents();
        QCOMPARE(h.plays(), 2);
    }
    void goesBackThroughItsSongs()
    {
        Harness h;
        QVERIFY(h.player.startFm());
        h.batch({1, 2, 3});
        QTRY_COMPARE(h.plays(), 1);
        h.publish(PlaybackState::Playing, 500);
        QVERIFY(!h.player.canGoPrevious()); // the first song, in its first seconds
        QVERIFY(h.player.next());
        QTRY_COMPARE(h.plays(), 2);
        h.publish(PlaybackState::Playing, 500);
        QCOMPARE(h.player.fmQueue().entries.size(), 2);
        QVERIFY(h.player.queue().entries.isEmpty()); // queue() stays the ordinary one's
        QVERIFY(h.player.canGoPrevious());
        QVERIFY(h.player.previous());
        QTRY_COMPARE(h.plays(), 3);
        QCOMPARE(h.play().query.song.value, "1");
        QCOMPARE(h.player.mode(), ListeningMode::PersonalFm);
        // On through the songs so far before a new one, at the end and by hand.
        h.publish(PlaybackState::Ended);
        QTRY_COMPARE(h.plays(), 4);
        QCOMPARE(h.play().query.song.value, "2");
        QVERIFY(h.player.next());
        QTRY_COMPARE(h.plays(), 5);
        QCOMPARE(h.play().query.song.value, "3");
        QCOMPARE(h.player.fmQueue().entries.size(), 3);
        QVERIFY(h.player.select(h.player.fmQueue().entries.first().id));
        QTRY_COMPARE(h.plays(), 6);
        QCOMPARE(h.play().query.song.value, "1");
        QCOMPARE(h.player.fmQueue().entries.size(), 3);
    }
    void restoredFmPlaysOn()
    {
        const auto saved = [] {
            QueueSnapshot queue;
            for (const char* id : {"1", "2"})
                queue.entries.append({QUuid::createUuid(), {QString::fromLatin1(id)}, {}});
            queue.active = queue.entries.last().id;
            return queue;
        }();
        Harness h;
        QVERIFY(h.player.restoreQueue(saved, h.f.snapshot(), h.player.intentRevision(),
                                      ListeningMode::PersonalFm));
        QVERIFY(h.player.restoredFm());
        QCOMPARE(h.player.queue().entries.size(), 2);
        QCOMPARE(h.plays(), 0);
        QVERIFY(h.player.resumeFm(saved.active));
        QTRY_COMPARE(h.plays(), 1);
        QCOMPARE(h.play().query.song.value, "2");
        QCOMPARE(h.player.mode(), ListeningMode::PersonalFm);
        QVERIFY(!h.player.restoredFm());
        QCOMPARE(h.player.fmQueue().entries.size(), 2);
        h.publish(PlaybackState::Playing, 500);
        QVERIFY(h.player.previous());
        QTRY_COMPARE(h.plays(), 2);
        QCOMPARE(h.play().query.song.value, "1");
        QVERIFY(h.player.next());
        QTRY_COMPARE(h.plays(), 3);
        QVERIFY(h.player.next()); // past the last: a new song
        QTRY_VERIFY(h.player.waitingForFm());
        QTRY_COMPARE(h.batches(), 1);
        h.batch({7});
        QTRY_COMPARE(h.plays(), 4);
        QCOMPARE(h.play().query.song.value, "7");
        QCOMPARE(h.player.fmQueue().entries.size(), 3);

        // From a new song at once: its songs so far stay.
        Harness fresh;
        QVERIFY(fresh.player.restoreQueue(saved, fresh.f.snapshot(), fresh.player.intentRevision(),
                                          ListeningMode::PersonalFm));
        QVERIFY(fresh.player.resumeFm({}));
        QTRY_COMPARE(fresh.batches(), 1);
        fresh.batch({8});
        QTRY_COMPARE(fresh.plays(), 1);
        QCOMPARE(fresh.play().query.song.value, "8");
        QCOMPARE(fresh.player.fmQueue().entries.size(), 3);
        // A guest's restored queue is never Personal FM.
        Harness guest(false);
        QVERIFY(!guest.player.resumeFm(saved.active));
    }
    void waitingBatchPreservesPause()
    {
        Harness h;
        QVERIFY(h.player.startFm());
        h.batch({1});
        QTRY_COMPARE(h.plays(), 1);
        QCOMPARE(h.batches(), 2);
        h.publish(PlaybackState::Ended);
        QTRY_VERIFY(h.player.waitingForFm());
        QVERIFY(!h.player.fmBlocked());
        QVERIFY(h.player.pause());
        h.batch({2});
        QTRY_COMPARE(h.plays(), 2);
        QCOMPARE(h.play().intent, TransportIntent::Pause);
        QCOMPARE(h.play().query.song.value, "2");
    }
    void failedRefillDrainsThenExplicitRetry()
    {
        Harness h;
        QSignalSpy stopped(&h.player, &ListeningSession::advanceStopped);
        QVERIFY(h.player.startFm());
        h.batch({1, 2, 3});
        QTRY_COMPARE(h.plays(), 1);
        h.deliver(h.fm(), MusicFailure{MusicError::Timeout});
        QCOMPARE(h.plays(), 1);
        for (int expected = 2; expected <= 3; ++expected) {
            h.publish(PlaybackState::Ended);
            QTRY_COMPARE(h.plays(), expected);
        }
        h.publish(PlaybackState::Ended);
        QTRY_VERIFY(h.player.fmBlocked());
        QCOMPARE(stopped.size(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(h.batches(), 2);
        QCOMPARE(h.plays(), 3);
        QVERIFY(h.player.retryFm());
        h.batch({});
        QTRY_VERIFY(h.player.fmBlocked());
        QCOMPARE(stopped.size(), 2);
        QVERIFY(h.player.retryFm());
        h.batch({4});
        QTRY_COMPARE(h.plays(), 4);
        QCOMPARE(h.play().query.song.value, "4");
    }
    void tenFailuresStopAndKeepFmSongs()
    {
        Harness h;
        QVERIFY(h.player.startFm());
        h.batch({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
        QTRY_COMPARE(h.plays(), 1);
        QSignalSpy stopped(&h.player, &ListeningSession::failureStreakExceeded);
        for (int attempt = 1; attempt <= 10; ++attempt) {
            h.publish(PlaybackState::Ready);
            h.publish(PlaybackState::Failed);
            if (attempt < 10)
                QTRY_COMPARE(h.plays(), attempt + 1);
        }
        QSignalSpy fmChanged(&h.player, &ListeningSession::fmChanged);
        QTRY_COMPARE(stopped.size(), 1);
        QVERIFY(!fmChanged.isEmpty());
        QVERIFY(h.player.fmBlocked());
        QCOMPARE(h.plays(), 10);
        QCOMPARE(h.player.fmQueue().entries.size(), 10);
        QVERIFY(h.player.retryFm());
        QTRY_COMPARE(h.plays(), 11);
        QCOMPARE(h.play().query.song.value, "11");
    }
    void switchQueueOnlyAfterAcceptance()
    {
        Harness h;
        QVERIFY(h.player.startFm());
        h.batch({1, 2, 3});
        QTRY_COMPARE(h.plays(), 1);
        const auto old = h.fm();
        h.fillMailbox();
        QVERIFY(!h.player.replaceQueue({{"9"}, {"10"}}));
        QCOMPARE(h.player.mode(), ListeningMode::PersonalFm);
        QVERIFY(h.source.snapshot().active);
        h.releaseMailbox();
        QVERIFY(h.player.replaceQueue({{"9"}, {"10"}}));
        QCOMPARE(h.player.mode(), ListeningMode::Queue);
        QCOMPARE(h.player.queue().entries.size(), 2);
        QVERIFY(!h.source.snapshot().active);
        PersonalFmBatch late;
        Song s;
        s.id = {"11"};
        late.songs.append(s);
        h.deliver(old, late);
        QCoreApplication::processEvents();
        QCOMPARE(h.play().query.song.value, "9");
    }
    void rejectedPlayRetainsTakenSong()
    {
        Harness h;
        QVERIFY(h.player.startFm());
        h.fillMailbox();
        h.batch({1, 2});
        // A failed control must not erase the already queued supply wakeup.
        QVERIFY(!h.player.stop());
        QVERIFY(!h.player.startFm());
        QTRY_VERIFY(h.player.fmBlocked());
        QCOMPARE(h.plays(), 0);
        QCOMPARE(h.source.snapshot().upcoming.first().id.value, "2");
        h.releaseMailbox();
        QVERIFY(h.player.retryFm());
        QTRY_COMPARE(h.plays(), 1);
        QCOMPARE(h.play().query.song.value, "1");
    }
    void stopCloseAndLateDelivery()
    {
        Harness h;
        QVERIFY(h.player.startFm());
        const auto old = h.fm();
        QVERIFY(h.player.stop());
        QCOMPARE(h.player.mode(), ListeningMode::Queue);
        QVERIFY(!h.source.snapshot().active);
        QVERIFY(!h.player.resume());
        PersonalFmBatch late;
        Song s;
        s.id = {"1"};
        late.songs.append(s);
        h.deliver(old, late);
        QCoreApplication::processEvents();
        QCOMPARE(h.plays(), 0);
        QVERIFY(h.player.startFm());
        h.f.close();
        QVERIFY(!h.player.waitingForFm());
        QVERIFY(!h.source.snapshot().active);
        QVERIFY(!h.player.startFm());
    }
    void logoutBeforeDeferredNotification()
    {
        Harness h;
        QVERIFY(h.player.startFm());
        h.batch({1});
        QTRY_COMPARE(h.plays(), 1);
        QVERIFY(h.f.signOut());
        QCOMPARE(h.player.mode(), ListeningMode::Queue);
        QVERIFY(!h.player.lyrics());
        QCOMPARE(h.player.playback().state, PlaybackState::Empty);
        QVERIFY(!h.player.next());
        QVERIFY(!h.player.retryFm());
        QCoreApplication::processEvents();
        QVERIFY(!h.source.snapshot().active);
        BackendEvent e;
        e.kind = BackendEvent::Kind::Session;
        e.generation = 1;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Authenticated, "2"};
        h.f.deliver(e);
        QCOMPARE(h.player.mode(), ListeningMode::Queue);
        QVERIFY(h.player.startFm());
        h.batch({1});
        QTRY_COMPARE(h.plays(), 2);
    }
    void reentrantQueueSelectionWhileTakingSong()
    {
        Harness h;
        QVERIFY(h.player.startFm());
        h.batch({1, 2, 3});
        bool once = true;
        const auto c =
            connect(&h.source, &PersonalFmSource::changed, &h.f, [&](PersonalFmSnapshot s) {
                if (once && s.fetching) {
                    once = false;
                    QVERIFY(h.player.replaceQueue({{"9"}}));
                }
            });
        QTRY_COMPARE(h.plays(), 1);
        QCOMPARE(h.play().query.song.value, "9");
        QCOMPARE(h.player.mode(), ListeningMode::Queue);
        disconnect(c);
    }
    void reentrantFmStartWinsOverQueueSwitch()
    {
        Harness h;
        QVERIFY(h.player.startFm());
        h.batch({1, 2, 3});
        QTRY_COMPARE(h.plays(), 1);
        bool once = true;
        const auto c =
            connect(&h.source, &PersonalFmSource::changed, &h.f, [&](PersonalFmSnapshot s) {
                if (once && !s.active) {
                    once = false;
                    QVERIFY(h.player.startFm(AudioQuality::Standard, TransportIntent::Pause));
                }
            });
        QVERIFY(h.player.replaceQueue({{"9"}}));
        QCOMPARE(h.player.mode(), ListeningMode::PersonalFm);
        QVERIFY(h.player.waitingForFm());
        disconnect(c);
        h.batch({4});
        QTRY_COMPARE(h.play().query.song.value, "4");
        QCOMPARE(h.play().intent, TransportIntent::Pause);
    }
    void externalFacadePlayDisownsFm()
    {
        Harness h;
        QVERIFY(h.player.startFm());
        h.batch({1, 2, 3});
        QTRY_COMPARE(h.plays(), 1);
        QVERIFY(h.f.playSong({"99"}));
        QCOMPARE(h.player.mode(), ListeningMode::Queue);
        QVERIFY(!h.source.snapshot().active);
        QVERIFY(h.player.queue().entries.isEmpty());
        QCOMPARE(h.play().query.song.value, "99");

        Harness nested;
        QVERIFY(nested.player.startFm());
        const auto c = connect(
            &nested.f, &BackendFacade::playbackRequested, &nested.f, [&](PlaybackCommand command) {
                if (command.action == PlaybackAction::Play && command.query.song.value == "1")
                    QVERIFY(nested.f.playSong({"99"}));
            });
        nested.batch({1, 2, 3});
        QTRY_COMPARE(nested.play().query.song.value, "99");
        QVERIFY(!nested.source.snapshot().active);
        QCOMPARE(nested.player.mode(), ListeningMode::Queue);
        QVERIFY(nested.player.queue().entries.isEmpty());
        disconnect(c);
    }
};
QTEST_GUILESS_MAIN(FmListeningTest)
#include "fm_listening_test.moc"
