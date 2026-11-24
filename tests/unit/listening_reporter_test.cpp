#include "application/backend/backend_facade.h"
#include "application/playback/listening_reporter.h"
#include "application/playback/listening_session.h"
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus;
using namespace application;
using namespace domain;
namespace
{
// An offline facade (no worker): playback commands are acknowledged, report commands
// collected; the test plays the backend's part with deliverPlayback.
struct Harness {
    BackendFacade facade;
    ListeningSession session{facade};
    ListeningReporter reporter{facade, session};
    QList<PlaybackCommand> commands;
    QList<ListeningReportCommand> reports;
    QList<ListeningReportCommand> actives; // ActiveClient, kept apart
    QDateTime now{QDate(2026, 11, 14), QTime(10, 0)};
    quint64 positionRevision = 0;
    explicit Harness(SessionPhase phase = SessionPhase::Authenticated)
    {
        reporter.setClock([this] { return now; });
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
        QObject::connect(&facade, &BackendFacade::listeningReportRequested, &facade,
                         [this](ListeningReportCommand c) {
                             (c.report.action == ListeningReportAction::ActiveClient ? actives
                                                                                     : reports)
                                 .append(c);
                         });
        facade.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        facade.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1},
                     phase,
                     phase == SessionPhase::Authenticated ? QString("101") : QString()};
        facade.deliver(e);
    }
    PlaybackCommand play() const
    {
        for (auto it = commands.crbegin(); it != commands.crend(); ++it)
            if (it->action == PlaybackAction::Play)
                return *it;
        return {};
    }
    // The backend's snapshot of the current load: `state`, with `playedMs` observed.
    void state(PlaybackState state, qint64 playedMs,
               SourceProvider provider = SourceProvider::Netease)
    {
        const auto c = play();
        PlaybackSnapshot p;
        p.token = c.token;
        p.song = c.query.song;
        p.state = state;
        p.intent = c.intent;
        p.positionRevision = ++positionRevision;
        p.durationMs = 200000;
        p.seekable = true;
        p.observed = ObservedPlayback{playedMs};
        p.provider = provider;
        facade.deliverPlayback({c.generation, p});
    }
    QList<ListeningReport> of(ListeningReportAction action) const
    {
        QList<ListeningReport> out;
        for (const auto& c : reports)
            if (c.report.action == action)
                out.append(c.report);
        return out;
    }
    QList<ListeningReport> plays() const { return of(ListeningReportAction::Play); }
    QList<ListeningReport> starts() const { return of(ListeningReportAction::StartPlay); }
    static void answer(BackendFacade& facade, const ListeningReportCommand& c,
                       ReportOutcome outcome)
    {
        facade.deliverListeningReport(
            {c.id,
             c.generation,
             c.context,
             {c.report.eventId, c.report.action, outcome,
              outcome == ReportOutcome::Acknowledged ? ReportError::None : ReportError::Network}});
    }
    void answer(qsizetype index) { answer(facade, reports[index], ReportOutcome::Acknowledged); }
    void answerActive(qsizetype index, ReportOutcome outcome = ReportOutcome::Acknowledged)
    {
        answer(facade, actives[index], outcome);
    }
};
} // namespace

class ListeningReporterTest : public QObject
{
    Q_OBJECT
  private slots:
    void reportsOnceWhenTheUserGoesOn()
    {
        Harness h;
        QHash<QUuid, ListeningReporter::Origin> origins;
        h.reporter.setOriginLookup([&](QUuid entry) { return origins.value(entry); });
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}}));
        const auto queue = h.session.queue();
        origins.insert(queue.entries[0].id, {ListeningReportSource::Album, "77"});
        h.state(PlaybackState::Playing, 12400);
        QVERIFY(h.plays().isEmpty()); // still playing
        QVERIFY(h.session.next());
        QCOMPARE(h.plays().size(), 1);
        const auto r = h.plays().first();
        QCOMPARE(r.song.value, "1");
        QCOMPARE(r.playedSeconds, 12);
        QCOMPARE(r.source, ListeningReportSource::Album);
        QCOMPARE(r.sourceId, "77");
        QCOMPARE(r.end, ListeningReportEnd::Ui);
        QCOMPARE(h.reports.first().accountId, "101");
    }
    void startPlayWhenFirstHeard()
    {
        Harness h;
        h.reporter.setOriginLookup([](QUuid) {
            return std::optional(ListeningReporter::Origin{ListeningReportSource::List, "55"});
        });
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}}));
        QVERIFY(h.reports.isEmpty()); // loading, nothing heard yet
        h.state(PlaybackState::Playing, 0);
        QVERIFY(h.reports.isEmpty());
        h.state(PlaybackState::Playing, 400);
        QCOMPARE(h.starts().size(), 1); // once, however long it plays
        h.state(PlaybackState::Playing, 9000);
        QCOMPARE(h.reports.size(), 1);
        const auto start = h.starts().first();
        QCOMPARE(start.song.value, "1");
        QCOMPARE(start.playedSeconds, 0);
        QCOMPARE(start.source, ListeningReportSource::List);
        QCOMPARE(start.sourceId, "55");
        QVERIFY(start.isValid());
        // Going on: the Play of the first, then the StartPlay of the next, as the web.
        QVERIFY(h.session.next());
        QTRY_COMPARE(h.play().query.song.value, "2");
        h.state(PlaybackState::Playing, 1000);
        QCOMPARE(h.reports.size(), 3);
        QCOMPARE(h.reports[1].report.action, ListeningReportAction::Play);
        QCOMPARE(h.reports[1].report.song.value, "1");
        QCOMPARE(h.reports[2].report.action, ListeningReportAction::StartPlay);
        QCOMPARE(h.reports[2].report.song.value, "2");
        // A short load has its StartPlay, never a Play.
        bool done = false;
        h.reporter.flush([&] { done = true; }, 50);
        QTRY_VERIFY(done);
        QCOMPARE(h.plays().size(), 1);
    }
    void endedFailedAndShortLoads()
    {
        Harness h;
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}, {"3"}}));
        h.state(PlaybackState::Playing, 2900);
        QVERIFY(h.session.next()); // under 3 s: not reported
        QVERIFY(h.plays().isEmpty());
        h.state(PlaybackState::Playing, 200000);
        h.state(PlaybackState::Ended, 200000);
        QCOMPARE(h.plays().size(), 1);
        QCOMPARE(h.plays().last().end, ListeningReportEnd::PlayEnd);
        QCOMPARE(h.plays().last().song.value, "2");
        // No origin known: the song itself.
        QCOMPARE(h.plays().last().source, ListeningReportSource::Song);
        QCOMPARE(h.plays().last().sourceId, "2");
        QTRY_COMPARE(h.play().query.song.value, "3"); // advanced
        h.state(PlaybackState::Playing, 5000);
        h.state(PlaybackState::Failed, 5000);
        QCOMPARE(h.plays().size(), 2);
        QCOMPARE(h.plays().last().end, ListeningReportEnd::Exception);
    }
    void longPauseSettlesOnce()
    {
        Harness h;
        h.reporter.setPauseTimeout(50);
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}}));
        h.state(PlaybackState::Playing, 8000);
        h.state(PlaybackState::Paused, 8000);
        QTRY_COMPARE(h.plays().size(), 1);
        QCOMPARE(h.plays().first().playedSeconds, 8);
        QCOMPARE(h.plays().first().end, ListeningReportEnd::Interrupt);
        // Playing on after that never reports this load again.
        h.state(PlaybackState::Playing, 20000);
        QVERIFY(h.session.next());
        QCOMPARE(h.plays().size(), 1);
        // A short pause is not a settlement.
        h.state(PlaybackState::Playing, 6000);
        h.state(PlaybackState::Paused, 6000);
        h.state(PlaybackState::Playing, 6000);
        QTest::qWait(120);
        QCOMPARE(h.plays().size(), 1);
    }
    void bilibiliRecordsNeverReportAfterSwitchingSongs()
    {
        Harness h;
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}}));
        h.state(PlaybackState::Playing, 60000, SourceProvider::Bilibili);
        QVERIFY(h.session.next());
        QVERIFY(h.reports.isEmpty());
        h.state(PlaybackState::Playing, 6000);
        QCOMPARE(h.starts().size(), 1);
        QCOMPARE(h.starts().first().song.value, "2");
        QVERIFY(h.session.stop());
        QCOMPARE(h.plays().size(), 1);
        QCOMPARE(h.plays().first().song.value, "2");
    }

    void guestsAndFmAreNotReported()
    {
        Harness guest(SessionPhase::Guest);
        QVERIFY(guest.session.replaceQueue({{"1"}, {"2"}}));
        guest.state(PlaybackState::Playing, 60000);
        QVERIFY(guest.session.next());
        QVERIFY(guest.reports.isEmpty()); // neither StartPlay nor Play
        QVERIFY(guest.actives.isEmpty());
    }
    void activeClientOnceADay()
    {
        QCOMPARE(ListeningReporter::dayStart({QDate(2026, 11, 15), QTime(5, 59)}),
                 QDateTime(QDate(2026, 11, 14), QTime(6, 0)));
        QCOMPARE(ListeningReporter::dayStart({QDate(2026, 11, 15), QTime(6, 0)}),
                 QDateTime(QDate(2026, 11, 15), QTime(6, 0)));
        Harness h;
        QCOMPARE(h.actives.size(), 1); // signed in
        QCOMPARE(h.actives.first().accountId, "101");
        QVERIFY(h.actives.first().report.isValid());
        h.answerActive(0);
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}, {"3"}, {"4"}}));
        h.state(PlaybackState::Playing, 4000);
        QCOMPARE(h.starts().size(), 1);
        QCOMPARE(h.actives.size(), 1); // the same day
        h.now = {QDate(2026, 11, 15), QTime(5, 59)};
        QVERIFY(h.session.next());
        QCOMPARE(h.actives.size(), 1); // still the day that began at 6:00 yesterday
        h.now = {QDate(2026, 11, 15), QTime(6, 0)};
        h.state(PlaybackState::Playing, 4000);
        QCOMPARE(h.actives.size(), 2); // a new day: before the next report
        QVERIFY(h.actives[1].id < h.reports.last().id);
        h.answerActive(1, ReportOutcome::Unknown);
        QVERIFY(h.session.next());
        QCOMPARE(h.actives.size(), 3); // not acknowledged: again before the next report
        h.answerActive(2);
        h.state(PlaybackState::Playing, 4000);
        QVERIFY(h.session.next());
        QCOMPARE(h.actives.size(), 3);
    }
    void neteaseThenBilibiliPauseAndFlushOnlyReportsNetease()
    {
        Harness h;
        h.answerActive(0);
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}}));
        h.state(PlaybackState::Playing, 30000);
        QVERIFY(h.session.next());
        h.state(PlaybackState::Playing, 30000, SourceProvider::Bilibili);
        h.state(PlaybackState::Paused, 30000, SourceProvider::Bilibili);
        bool done = false;
        h.reporter.flush([&] { done = true; }, 50);
        QTRY_VERIFY(done);
        QCOMPARE(h.starts().size(), 1);
        QCOMPARE(h.plays().size(), 1);
        for (const auto& report : h.reports)
            QCOMPARE(report.report.song.value, QString("1"));
        QCOMPARE(h.session.history().size(), 2);
    }
    void flushSettlesThePlayingLoadAndWaits()
    {
        Harness h;
        QVERIFY(h.session.replaceQueue({{"1"}}));
        h.state(PlaybackState::Playing, 30000);
        bool done = false;
        h.reporter.flush([&] { done = true; }, 5000);
        QCOMPARE(h.plays().size(), 1);
        QCOMPARE(h.plays().first().end, ListeningReportEnd::Interrupt);
        QTest::qWait(50);
        QVERIFY(!done); // waits for the answers
        h.answerActive(0);
        for (qsizetype i = 0; i < h.reports.size(); ++i)
            h.answer(i);
        QVERIFY(done);
        // Without an answer it gives up after the timeout.
        Harness late;
        QVERIFY(late.session.replaceQueue({{"1"}}));
        late.state(PlaybackState::Playing, 30000);
        bool lateDone = false;
        late.reporter.flush([&] { lateDone = true; }, 50);
        QTRY_VERIFY(lateDone);
        // Nothing to settle: done straight away (but not inside the call).
        Harness idle;
        idle.answerActive(0);
        bool idleDone = false;
        idle.reporter.flush([&] { idleDone = true; });
        QVERIFY(!idleDone);
        QTRY_VERIFY(idleDone);
    }
    void searchKeywordsAreTheSourceId()
    {
        Harness h;
        h.reporter.setOriginLookup([](QUuid) {
            return std::optional(
                ListeningReporter::Origin{ListeningReportSource::Search, "富士山下"});
        });
        QVERIFY(h.session.replaceQueue({{"1"}, {"2"}}));
        h.state(PlaybackState::Playing, 4000);
        QVERIFY(h.session.next());
        QCOMPARE(h.plays().first().source, ListeningReportSource::Search);
        QCOMPARE(h.plays().first().sourceId, "富士山下");
        // An origin that isn't valid falls back to the song.
        h.reporter.setOriginLookup([](QUuid) {
            return std::optional(ListeningReporter::Origin{ListeningReportSource::Album, ""});
        });
        h.state(PlaybackState::Playing, 4000);
        h.state(PlaybackState::Ended, 4000);
        QCOMPARE(h.plays().last().source, ListeningReportSource::Song);
    }
    void aProgramIsReportedFromItsPodcast()
    {
        Harness h;
        h.reporter.setOriginLookup([](QUuid) { // a song's lookup never applies to a program
            return std::optional(ListeningReporter::Origin{ListeningReportSource::List, "55"});
        });
        const PlayOrigin podcast{PlayOriginKind::Podcast, "4", "synthetic podcast"};
        QVERIFY(h.session.replaceQueue({{"99"}, {"98"}}, 0, AudioQuality::Standard, {podcast, {}},
                                       {{"5"}, {"6"}}));
        h.state(PlaybackState::Playing, 4000);
        QCOMPARE(h.starts().size(), 1);
        QVERIFY(h.session.next());
        const auto r = h.plays().first();
        QCOMPARE(r.song.value, "99"); // the track
        QCOMPARE(r.program.value, "5");
        QCOMPARE(r.source, ListeningReportSource::DjRadio);
        QCOMPARE(r.sourceId, "4");
        QVERIFY(r.isValid());
        h.state(PlaybackState::Playing, 4000);
        h.state(PlaybackState::Ended, 4000);
        QCOMPARE(h.plays().last().program.value, "6");
        QCOMPARE(h.plays().last().sourceId, ""); // no podcast known: none made up
        QVERIFY(h.plays().last().isValid());
    }
};
QTEST_GUILESS_MAIN(ListeningReporterTest)
#include "listening_reporter_test.moc"
