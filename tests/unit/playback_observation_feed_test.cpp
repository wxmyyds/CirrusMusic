#include "infrastructure/ipc/playback_observation_feed.h"
#include <QSignalSpy>
#include <QtTest>
#include <thread>

using namespace cirrus;
using namespace application;
using namespace domain;
using namespace infrastructure::ipc;
namespace
{
struct Harness {
    BackendFacade facade;
    ListeningSession owner{facade};
    PlaybackObservationPublisher publisher{owner, facade};
    QList<PlaybackCommand> commands;
    QList<MusicCommand> reads;
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
        QObject::connect(&facade, &BackendFacade::musicRequested, &facade,
                         [this](MusicCommand c) { reads.append(c); });
        facade.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        facade.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest, {}};
        facade.deliver(e);
    }
    PlaybackCommand play() const
    {
        for (auto it = commands.crbegin(); it != commands.crend(); ++it)
            if (it->action == PlaybackAction::Play)
                return *it;
        return {};
    }
    void state(qint64 position = 0, quint64 revision = 1)
    {
        const auto c = play();
        facade.deliverPlaybackSource({c.token, c.query.song, SourceProvider::Netease, {}});
        PlaybackSnapshot p;
        p.token = c.token;
        p.song = c.query.song;
        p.state = PlaybackState::Playing;
        p.intent = TransportIntent::Play;
        p.positionMs = position;
        p.durationMs = 10000;
        p.seekable = true;
        p.positionRevision = revision;
        p.seekRevision = revision;
        facade.deliverPlayback({c.generation, p});
    }
    void session(SessionPhase phase, QString account = {})
    {
        BackendEvent e;
        e.kind = BackendEvent::Kind::Session;
        e.session = facade.snapshot();
        e.session.phase = phase;
        e.session.accountId = account;
        facade.deliver(e);
    }
    void lyrics()
    {
        const auto selected = play();
        facade.deliverPlaybackSource({selected.token, selected.query.song, SourceProvider::Netease, {}});
        QVERIFY(!reads.isEmpty());
        SongLyrics l;
        l.song = play().query.song;
        l.original.standard.availability = LyricAvailability::Present;
        l.original.standard.timing = LyricTiming::Lines;
        l.original.standard.lines.append({0, 10000, "synthetic 雲", {}});
        const auto c = reads.last();
        facade.deliverMusic({c.id, c.generation, c.context, std::move(l)});
    }
};
struct Sink final : IObservationWriteSink {
    bool closed = false;
    QByteArray bytes;
    qint64 pendingBytes() const override { return 0; }
    qint64 write(QByteArrayView b) override
    {
        bytes.append(b.data(), b.size());
        return b.size();
    }
    void closeAndDiscard() override
    {
        closed = true;
        bytes.clear();
    }
};
} // namespace
class PlaybackObservationFeedTest final : public QObject
{
    Q_OBJECT
  private slots:
    void disabledFreshSourceAndNoCommands()
    {
        Harness h;
        PlaybackObservationFeed feed(h.publisher);
        QSignalSpy wakes(&feed, &PlaybackObservationFeed::wakeRequested);
        QVERIFY(!feed.enabled());
        QVERIFY(!feed.mailbox()->take());
        QVERIFY(!feed.wakeTicket()->pending());
        QTRY_COMPARE(h.publisher.snapshot().status, ObservationStatus::Ready);
        QVERIFY(!feed.mailbox()->take());
        QCOMPARE(wakes.size(), 0);
        QVERIFY(h.commands.isEmpty());
        QVERIFY(h.reads.isEmpty());
        QVERIFY(feed.setEnabled(true));
        auto value = feed.mailbox()->take();
        QVERIFY(value);
        QCOMPARE(value->snapshot.status, ObservationStatus::Ready);
        QCOMPARE(wakes.size(), 1);
        QVERIFY(!value->document);
    }
    void validatedFactoryBeforeFirstFlush()
    {
        Harness h;
        QVERIFY(!h.publisher.validatedExport());
        PlaybackObservationFeed feed(h.publisher);
        QVERIFY(feed.setEnabled(true));
        QVERIFY(!feed.mailbox()->take());
        QTRY_VERIFY(h.publisher.validatedExport().has_value());
        QVERIFY(feed.mailbox()->take());
    }
    void latestSharedDocumentAndScalarWake()
    {
        Harness h;
        PlaybackObservationFeed feed(h.publisher);
        QSignalSpy wakes(&feed, &PlaybackObservationFeed::wakeRequested);
        QVERIFY(h.owner.replaceQueue({{"1"}}));
        h.state();
        h.lyrics();
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        const auto commandCount = h.commands.size(), readCount = h.reads.size();
        QVERIFY(feed.setEnabled(true));
        auto first = feed.mailbox()->take();
        QVERIFY(first && first->document);
        const auto document = first->document;
        QCOMPARE(wakes.size(), 1);
        QVERIFY(feed.wakeTicket()->pending());
        for (int i = 1; i <= 4; ++i) {
            const auto seq = h.publisher.snapshot().sequence;
            h.state(i, i + 1);
            QTRY_VERIFY(h.publisher.snapshot().sequence > seq);
            auto value = feed.mailbox()->take();
            QVERIFY(value);
            QCOMPARE(value->document.get(), document.get());
            QCOMPARE(value->snapshot.positionMs, std::optional<qint64>(i));
        }
        // take/periodic pump does not dequeue the still queued notification ticket.
        QCOMPARE(wakes.size(), 1);
        QCOMPARE(h.commands.size(), commandCount);
        QCOMPARE(h.reads.size(), readCount);
        feed.wakeTicket()->acknowledge();
        const auto seq = h.publisher.snapshot().sequence;
        h.state(9, 9);
        QTRY_VERIFY(h.publisher.snapshot().sequence > seq);
        QCOMPARE(wakes.size(), 2);
        QVERIFY(feed.wakeTicket()->pending());
    }
    void privateIdentityChange_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("phase") << 0;
        QTest::newRow("original-token") << 1;
        QTest::newRow("same-token-account") << 2;
    }
    void privateIdentityChange()
    {
        QFETCH(int, change);
        Harness h;
        if (change == 2)
            h.session(SessionPhase::Authenticated, "synthetic-A");
        PlaybackObservationFeed feed(h.publisher);
        QVERIFY(h.owner.replaceQueue({{"1"}}));
        h.state();
        h.lyrics();
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        QVERIFY(feed.setEnabled(true));
        auto old = feed.mailbox()->take();
        QVERIFY(old && old->document);
        const auto fence = feed.mailbox()->fence();
        if (change == 0)
            h.session(SessionPhase::Authenticating);
        else if (change == 2)
            h.session(SessionPhase::Authenticated, "synthetic-B");
        else {
            BackendEvent e;
            e.kind = BackendEvent::Kind::Session;
            e.session = h.facade.snapshot();
            e.session.token = {QUuid::createUuid(), 2};
            h.facade.deliver(e);
        }
        QVERIFY(feed.mailbox()->fence() > fence);
        QVERIFY(!feed.mailbox()->take());
        QVERIFY(!h.publisher.validatedExport());
        h.publisher.changed(old->snapshot);
        QVERIFY(!feed.mailbox()->take());
    }
    void synchronousPhaseBackAndHistoricalReplay()
    {
        Harness h;
        PlaybackObservationFeed feed(h.publisher);
        QVERIFY(h.owner.replaceQueue({{"1"}}));
        h.state();
        h.lyrics();
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        QVERIFY(feed.setEnabled(true));
        auto old = feed.mailbox()->take();
        QVERIFY(old && old->document);
        feed.wakeTicket()->acknowledge();
        const auto fence = feed.mailbox()->fence();
        h.session(SessionPhase::Authenticating);
        QVERIFY(feed.mailbox()->fence() > fence);
        QVERIFY(!feed.mailbox()->take());
        QVERIFY(!h.publisher.validatedExport());
        const auto after = feed.mailbox()->fence();
        h.session(SessionPhase::Guest);
        QVERIFY(feed.mailbox()->fence() > after);
        QVERIFY(!feed.mailbox()->take());
        h.publisher.changed(old->snapshot);
        QVERIFY(!feed.mailbox()->take());
        QTest::qWait(1);
        QVERIFY(!feed.mailbox()->take());
        QVERIFY(h.owner.setVolume(0.25f));
        h.publisher.changed(old->snapshot);
        QVERIFY(!feed.mailbox()->take());         // control-only revision cannot reauthorize
        QVERIFY(h.owner.queue().active.isNull()); // a phase change resets the queue
        QVERIFY(h.owner.replaceQueue({{"1"}}));
        h.state();
        QTRY_VERIFY(h.publisher.validatedExport().has_value());
        auto current = feed.mailbox()->take();
        QVERIFY(current);
        QVERIFY(current->snapshot.binding.viewEpoch != old->snapshot.binding.viewEpoch);
        QVERIFY(!current->document);
    }
    void barrierBeforeFirstSamplePendingOwnerLoad()
    {
        Harness h;
        PlaybackObservationFeed feed(h.publisher);
        QVERIFY(h.owner.replaceQueue({{"1"}}));
        const auto pending = h.play();
        QVERIFY(pending.token.generation);
        QVERIFY(feed.setEnabled(true));
        QVERIFY(!feed.mailbox()->take());
        const auto fence = feed.mailbox()->fence();
        h.session(SessionPhase::Authenticating);
        h.session(SessionPhase::Guest);
        QVERIFY(feed.mailbox()->fence() > fence);
        h.state();
        QTest::qWait(1);
        QVERIFY(!h.publisher.validatedExport());
        QVERIFY(!feed.mailbox()->take());
        QVERIFY(h.owner.replaceQueue(
            {{"1"}})); // a phase change resets the queue (55458e5): a fresh load
        h.state();
        QTRY_VERIFY(h.publisher.validatedExport().has_value());
        QVERIFY(feed.mailbox()->take());
    }
    void policyOffOnRetiresOriginalLoad()
    {
        Harness h;
        PlaybackObservationFeed feed(h.publisher);
        QVERIFY(h.owner.replaceQueue({{"1"}}));
        h.state();
        QTRY_VERIFY(h.publisher.snapshot().song.has_value());
        QVERIFY(feed.setEnabled(true));
        auto old = feed.mailbox()->take();
        QVERIFY(old);
        const auto fence = feed.mailbox()->fence();
        QVERIFY(feed.setEnabled(false));
        QVERIFY(feed.mailbox()->fence() > fence);
        QVERIFY(!feed.mailbox()->take());
        QVERIFY(feed.setEnabled(true));
        h.publisher.changed(old->snapshot);
        QVERIFY(!feed.mailbox()->take());
        QTest::qWait(1);
        QVERIFY(!h.publisher.validatedExport());
        QVERIFY(h.owner.select(h.owner.queue().active));
        h.state();
        QTRY_VERIFY(h.publisher.validatedExport().has_value());
        auto next = feed.mailbox()->take();
        QVERIFY(next);
        QVERIFY(next->snapshot.binding.viewEpoch != old->snapshot.binding.viewEpoch);
    }
    void reentrantOlderChangedCannotAuthorize()
    {
        Harness h;
        bool once = false;
        // This observer precedes the feed's connection, changing private context
        // while delivery of a now-historical changed argument is still in progress.
        QObject::connect(&h.publisher, &PlaybackObservationPublisher::changed, &h.publisher,
                         [&](PlaybackObservation p) {
                             if (!once && p.song) {
                                 once = true;
                                 h.session(SessionPhase::Authenticating);
                                 h.session(SessionPhase::Guest);
                             }
                         });
        PlaybackObservationFeed feed(h.publisher);
        QVERIFY(feed.setEnabled(true));
        QVERIFY(h.owner.replaceQueue({{"1"}}));
        h.state();
        QTRY_VERIFY(once);
        QVERIFY(!feed.mailbox()->take());
    }
    void reentrantBarrierSamplingWaitsForRetirementEnd()
    {
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"1"}}));
        h.state();
        QTRY_VERIFY(h.publisher.validatedExport().has_value());
        bool once = false, selected = false, authorizedInside = false;
        QObject::connect(
            &h.publisher, &PlaybackObservationPublisher::contextRetired, &h.publisher,
            [&](ObservationBinding) {
                if (once)
                    return;
                once = true;
                h.session(SessionPhase::Guest);
                selected = h.owner.replaceQueue(
                    {{"1"}}); // a phase change resets the queue (55458e5): a fresh load
                h.state(4, 4);
                QCoreApplication::processEvents(); // nested coherent capture
                authorizedInside = h.publisher.validatedExport().has_value();
            });
        PlaybackObservationFeed feed(h.publisher);
        QVERIFY(feed.setEnabled(true));
        auto old = feed.mailbox()->take();
        QVERIFY(old);
        h.session(SessionPhase::Authenticating);
        QVERIFY(once && selected);
        QVERIFY(!authorizedInside);
        QVERIFY(!feed.mailbox()->take());
        QTRY_VERIFY(h.publisher.validatedExport().has_value());
        // The nested sample was not exported during retirement. Await the
        // scheduled post-barrier publication, not merely a now-live getter.
        QTRY_VERIFY(feed.mailbox()->take().has_value());
        auto current = feed.mailbox()->take();
        QVERIFY(current && current->snapshot.song);
        QVERIFY(current->snapshot.binding.viewEpoch != old->snapshot.binding.viewEpoch);
    }
    void forgedArgumentUsesOnlyLiveOwnerFactory()
    {
        Harness h;
        PlaybackObservationFeed feed(h.publisher);
        QTRY_COMPARE(h.publisher.snapshot().status, ObservationStatus::Ready);
        QVERIFY(feed.setEnabled(true));
        auto first = feed.mailbox()->take();
        QVERIFY(first);
        auto forged = first->snapshot;
        forged.binding.viewEpoch = QUuid::createUuid();
        forged.sequence += 99;
        forged.song = SongId{"999"};
        forged.metadata = PlaybackMetadataView{QString{"forged"}, {}, {}};
        h.publisher.changed(forged);
        const auto repeated = feed.mailbox()->take(); // take retains latest, not a queue pop
        QVERIFY(repeated);
        QCOMPARE(repeated->snapshot.binding, first->snapshot.binding);
        QCOMPARE(repeated->snapshot.sequence, first->snapshot.sequence);
        QVERIFY(!repeated->snapshot.song && !repeated->snapshot.metadata);
        QVERIFY(!feed.mailbox()->wakePending()); // duplicate live sequence, no new publication
        QVERIFY(feed.setEnabled(false));
        h.publisher.changed(forged);
        QVERIFY(!feed.mailbox()->take());
    }
    void privateFenceClosesLogicalClientBeforeWrite()
    {
        Harness h;
        PlaybackObservationFeed feed(h.publisher);
        QTRY_COMPARE(h.publisher.snapshot().status, ObservationStatus::Ready);
        QVERIFY(feed.setEnabled(true));
        Sink sink;
        PlaybackObservationConnections engine(feed.mailbox());
        const auto id = engine.open(sink, 0);
        QVERIFY(id);
        feed.wakeTicket()->acknowledge();
        h.session(SessionPhase::Authenticating);
        engine.write(id, 1);
        QVERIFY(sink.closed);
        QVERIFY(!engine.isOpen(id));
        QVERIFY(sink.bytes.isEmpty());
        QCOMPARE(engine.lastClose(), ObservationClose::Privacy);
    }
    void backendShutdownAndPublisherCloseAreImmediate()
    {
        Harness h;
        PlaybackObservationFeed feed(h.publisher);
        QTRY_COMPARE(h.publisher.snapshot().status, ObservationStatus::Ready);
        QVERIFY(feed.setEnabled(true));
        QVERIFY(feed.mailbox()->take());
        const auto fence = feed.mailbox()->fence();
        h.facade.close();
        QVERIFY(feed.mailbox()->fence() > fence);
        QVERIFY(!feed.mailbox()->take());
        QVERIFY(!h.publisher.validatedExport());
        const auto next = feed.mailbox()->fence();
        h.publisher.close();
        QVERIFY(feed.mailbox()->fence() > next);
        QVERIFY(!feed.mailbox()->take());
    }
    void closeIsOneShotAndDoesNotStopOwner()
    {
        Harness h;
        PlaybackObservationFeed feed(h.publisher);
        QVERIFY(h.owner.replaceQueue({{"1"}}));
        h.state();
        QTRY_VERIFY(h.publisher.snapshot().song.has_value());
        QVERIFY(feed.setEnabled(true));
        QVERIFY(feed.mailbox()->take());
        const auto commands = h.commands.size(), reads = h.reads.size();
        const auto fence = feed.mailbox()->fence();
        feed.close();
        QVERIFY(feed.mailbox()->fence() > fence);
        const auto after = feed.mailbox()->fence();
        feed.close();
        QCOMPARE(feed.mailbox()->fence(), after);
        QVERIFY(!feed.setEnabled(true));
        QCOMPARE(h.commands.size(), commands);
        QCOMPARE(h.reads.size(), reads);
        QVERIFY(h.publisher.snapshot().song.has_value());
    }
    void destroyedPublisherRetiresPinnedSource()
    {
        Harness h;
        auto publisher = std::make_unique<PlaybackObservationPublisher>(h.owner, h.facade);
        PlaybackObservationFeed feed(*publisher);
        QTRY_COMPARE(publisher->snapshot().status, ObservationStatus::Ready);
        QVERIFY(feed.setEnabled(true));
        auto source = feed.mailbox();
        QVERIFY(source->take());
        const auto fence = source->fence();
        publisher.reset();
        QVERIFY(source->fence() > fence);
        QVERIFY(!source->take());
        QVERIFY(!feed.enabled());
        QVERIFY(!feed.setEnabled(true));
    }
    void foreignThreadFactoryAndEnableFailClosed()
    {
        Harness h;
        PlaybackObservationFeed feed(h.publisher);
        QTRY_COMPARE(h.publisher.snapshot().status, ObservationStatus::Ready);
        bool factory = false, enable = false;
        std::thread other([&] {
            factory = h.publisher.validatedExport().has_value();
            enable = feed.setEnabled(true);
        });
        other.join();
        QVERIFY(!factory);
        QVERIFY(!enable);
        QVERIFY(!feed.enabled());
        QVERIFY(!feed.mailbox()->take());
    }
    void wakeTicketAcknowledgedAtCallbackStart()
    {
        auto ticket = std::make_shared<ObservationWakeTicket>();
        QVERIFY(ticket->request());
        QVERIFY(!ticket->request());
        std::thread consumer([&] { ticket->acknowledge(); });
        consumer.join();
        QVERIFY(!ticket->pending());
        QVERIFY(ticket->request());
        QVERIFY(!ticket->request());
    }
};
QTEST_GUILESS_MAIN(PlaybackObservationFeedTest)
#include "playback_observation_feed_test.moc"
