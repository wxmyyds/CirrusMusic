#include "../support/fake_playback.h"
#include "application/playback/playback_coordinator.h"
#include "application/playback/system_media_commands.h"
#include <QSignalSpy>
#include <QtTest>
#include <limits>

using namespace cirrus;
using namespace application;
using namespace domain;
namespace
{
struct Harness {
    BackendFacade facade;
    ListeningSession owner{facade};
    PlaybackObservationPublisher publisher{owner, facade};
    SystemMediaCommands bridge{owner, publisher};
    QList<PlaybackCommand> commands;
    QList<MusicCommand> reads;
    quint64 positionRevision = 0;
    Harness()
    {
        QObject::connect(&facade, &BackendFacade::playbackRequested, &facade,
                         [this](PlaybackCommand c) {
                             if (c.action != PlaybackAction::Vocals) // a side note for mixing
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
    void selectSource()
    {
        const auto c = play();
        facade.deliverPlaybackSource({c.token, c.query.song, SourceProvider::Netease, {}});
    }
    void state(PlaybackState state, qint64 position = 0, std::optional<PreviewRange> preview = {},
               quint64 jump = 0)
    {
        const auto c = play();
        selectSource();
        PlaybackSnapshot p;
        p.token = c.token;
        p.song = c.query.song;
        p.state = state;
        p.intent = c.intent;
        p.positionMs = position;
        p.positionRevision = ++positionRevision;
        p.seekRevision = jump;
        p.preview = preview;
        p.durationMs = 10000;
        p.seekable = true;
        facade.deliverPlayback({c.generation, p});
    }
    void lyrics(SongLyrics l)
    {
        selectSource();
        QVERIFY(!reads.isEmpty());
        const auto r = reads.last();
        facade.deliverMusic({r.id, r.generation, r.context, std::move(l)});
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
};
LyricTrack timed(std::initializer_list<qint64> starts)
{
    LyricTrack t;
    t.availability = LyricAvailability::Present;
    t.timing = LyricTiming::Lines;
    for (auto s : starts)
        t.lines.append({s, 5000, " synthetic 雲 & <plain> ", {}});
    return t;
}
SongLyrics lyric(QString id = "9007199254740993")
{
    SongLyrics l;
    l.song = {id};
    l.original.standard = timed({0, 1000, 1000});
    l.original.enhanced = timed({0});
    l.original.enhanced.timing = LyricTiming::Words;
    l.original.enhanced.lines[0].words = {{0, 1500, "one "}, {1000, 2000, "雲"}};
    l.translation.standard = timed({1500});
    l.translation.enhanced.availability = LyricAvailability::Empty;
    l.romanization.standard.availability = LyricAvailability::Malformed;
    l.romanization.standard.malformedLines = 1;
    l.romanization.enhanced.availability = LyricAvailability::Present;
    l.romanization.enhanced.lines = {{{}, {}, "untimed", {}}};
    l.original.standard.credits = {"synthetic private credits not exported"};
    l.providerNoLyric = false;
    l.instrumental = false;
    return l;
}
} // namespace
class PlaybackObservationTest final : public QObject
{
    Q_OBJECT
  private slots:
    void emptyAndNoSideEffects()
    {
        Harness h;
        QTRY_COMPARE(h.publisher.snapshot().status, ObservationStatus::Ready);
        const auto p = h.publisher.snapshot();
        QVERIFY(p.binding.trackId.isNull());
        QVERIFY(!p.positionMs && !p.volumeLinear && !p.metadata);
        QVERIFY(!h.publisher.document(p.binding, p.lyricsRevision));
        QVERIFY(h.commands.isEmpty());
        QVERIFY(h.reads.isEmpty());
    }
    void destroyedOwnerRejectsQueuedCommands()
    {
        auto h = std::make_unique<Harness>();
        auto p = std::make_unique<PlaybackObservationPublisher>(h->owner, h->facade);
        auto b = std::make_unique<SystemMediaCommands>(h->owner, *p);
        QVERIFY(h->owner.replaceQueue({{"1"}}));
        QTRY_VERIFY(p->snapshot().song.has_value());
        QSignalSpy finished(b.get(), &SystemMediaCommands::finished);
        QVERIFY(b->submit(p->snapshot().binding, {SystemMediaAction::Volume, 0, 0.5f}));
        h.reset(); // facade outlives owner inside Harness; external observers survive both
        QCOMPARE(p->snapshot().status, ObservationStatus::Unavailable);
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished.first()[1].toBool());
        QTest::qWait(1);
        QCOMPARE(finished.size(), 1); // no late callback or duplicate terminal result
        QVERIFY(!b->submit(p->snapshot().binding, {SystemMediaAction::Play}));
    }
    void coherentSixTracksAndPreview()
    {
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}}));
        h.state(PlaybackState::Playing, 1250);
        h.lyrics(lyric());
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        auto p = h.publisher.snapshot();
        QCOMPARE(p.song->value, "9007199254740993");
        QCOMPARE(*p.positionMs, 1250);
        QCOMPARE(*p.songPositionMs, 1250);
        QCOMPARE(p.cursors->original.standard.lines, (QList<qsizetype>{0, 1, 2}));
        QCOMPARE(p.cursors->original.enhanced.words.size(), 2);
        QVERIFY(p.cursors->translation.standard.lines.isEmpty());
        QVERIFY(p.cursors->romanization.enhanced.lines.isEmpty());
        QVERIFY(p.positionObservedMonoMs);
        QVERIFY(!p.metadata && !p.volumeLinear);
        auto d = h.publisher.document(p.binding, p.lyricsRevision);
        QVERIFY(d);
        QVERIFY(d->lyrics.original.standard.credits.isEmpty());
        QVERIFY(!h.owner.lyrics()->original.standard.credits.isEmpty());
        QCOMPARE(d->lyrics.original.standard.lines[0].text, " synthetic 雲 & <plain> ");
        QCOMPARE(d->lyrics.translation.enhanced.availability, LyricAvailability::Empty);
        QVERIFY(p.lyricTracks);
        QCOMPARE((*p.lyricTracks)[3].availability, LyricAvailability::Empty);
        QCOMPARE((*p.lyricTracks)[4].malformedLines, qsizetype(1));
        QCOMPARE(p.providerNoLyric, std::optional<bool>(false));
        QCOMPARE(d->lyrics.romanization.standard.availability, LyricAvailability::Malformed);
        QCOMPARE(d->lyrics.romanization.enhanced.timing, LyricTiming::PlainText);
        h.state(PlaybackState::Paused, 2000, PreviewRange{1000, 11000}, 1);
        QTRY_COMPARE(h.publisher.snapshot().seekRevision, quint64(1));
        p = h.publisher.snapshot();
        QCOMPARE(*p.songPositionMs, 3000);
        QCOMPARE(p.cursors->translation.standard.lines, QList<qsizetype>{0});
        const auto revision = p.lyricsRevision;
        QVERIFY(h.owner.setLyricOffset(120000));
        QTRY_COMPARE(h.publisher.snapshot().userOffsetMs, 120000);
        QCOMPARE(h.publisher.snapshot().lyricsRevision, revision);
        QVERIFY(h.publisher.snapshot().cursors->original.standard.lines.isEmpty());
        const auto sequence = h.publisher.snapshot().sequence;
        QVERIFY(h.owner.setLyricOffset(119999)); // still empty: separate offset notification
        QTRY_VERIFY(h.publisher.snapshot().sequence > sequence);
        QCOMPARE(h.reads.size(), 1); // no extra fetch; only existing owner play-time lyric read
        QCOMPARE(h.commands.size(), 1);
    }
    void readingDuringOwnerSignalIsNotMixed()
    {
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}}));
        h.state(PlaybackState::Playing, 0);
        h.lyrics(lyric());
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        auto before = h.publisher.snapshot();
        PlaybackObservation during;
        connect(&h.owner, &ListeningSession::playbackChanged, &h.owner,
                [&](PlaybackSnapshot) { during = h.publisher.snapshot(); });
        h.state(PlaybackState::Playing, 2000);
        QCOMPARE(during.positionMs, before.positionMs);
        QCOMPARE(during.cursors, before.cursors);
        QTRY_COMPARE(h.publisher.snapshot().positionMs, std::optional<qint64>(2000));
        QCOMPARE(h.publisher.snapshot().cursors->translation.standard.lines, QList<qsizetype>{0});
    }
    void metadataIsExplicitAndHasNoUrls()
    {
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}}));
        QTRY_VERIFY(h.publisher.snapshot().song.has_value());
        const auto p = h.publisher.snapshot();
        Song s;
        s.id = *p.song;
        s.title = " synthetic title <not rich> ";
        s.artists = {{{}, "same"}, {{}, "same"}};
        s.album = AlbumReference{{}, "synthetic album", QUrl("file:///synthetic/private-cover")};
        QVERIFY(h.publisher.attachMetadata(p.binding, s));
        QTRY_VERIFY(h.publisher.snapshot().metadata.has_value());
        QCOMPARE(h.publisher.snapshot().metadata->artists, (QStringList{"same", "same"}));
        QCOMPARE(h.publisher.snapshot().metadata->title, s.title);
        QCOMPARE(h.publisher.snapshot().metadataStatus, ObservationContentStatus::Ready);
        s.id = {"2"};
        QVERIFY(!h.publisher.attachMetadata(p.binding, s));
        s.id = *p.song;
        s.title = QString(4097, u'x');
        QVERIFY(!h.publisher.attachMetadata(p.binding, s));
        QTRY_COMPARE(h.publisher.snapshot().metadataStatus,
                     ObservationContentStatus::LimitExceeded);
        QVERIFY(!h.publisher.snapshot().metadata);
        QCOMPARE(h.reads.size(), 0); // metadata attachment never selects a source or fetches lyrics
    }
    void sameSongNewLoadAndLateLyrics()
    {
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}, {"9007199254740993"}}));
        h.lyrics(lyric());
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        const auto a = h.publisher.snapshot();
        const auto old = h.reads.last();
        QVERIFY(h.owner.next());
        QVERIFY(!h.publisher.document(a.binding, a.lyricsRevision));
        QVERIFY(!h.publisher.snapshot().song); // no immediate old-content adoption
        h.facade.deliverMusic({old.id, old.generation, old.context, lyric()});
        QTRY_VERIFY(h.publisher.snapshot().song.has_value());
        const auto b = h.publisher.snapshot();
        QVERIFY(a.binding.trackId != b.binding.trackId);
        QVERIFY(a.entry != b.entry);
        QVERIFY(!b.cursors && !b.metadata);
        QCOMPARE(b.lyricsState, LyricsState::Loading);
    }
    void privacy_data()
    {
        QTest::addColumn<int>("mode");
        for (int n = 0; n < 5; ++n)
            QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void privacy()
    {
        QFETCH(int, mode);
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}}));
        h.lyrics(lyric());
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        const auto a = h.publisher.snapshot();
        if (mode == 0)
            h.session(SessionPhase::Authenticating);
        if (mode == 1)
            h.session(SessionPhase::Committing);
        if (mode == 2)
            h.session(SessionPhase::Expired);
        if (mode == 3)
            h.session(SessionPhase::Guest, "synthetic-other-account");
        if (mode == 4)
            h.facade.close();
        QCOMPARE(h.publisher.snapshot().status, ObservationStatus::Unavailable);
        QVERIFY(!h.publisher.document(a.binding, a.lyricsRevision));
        QTRY_VERIFY(h.publisher.snapshot().binding.viewEpoch != a.binding.viewEpoch);
        QCOMPARE(h.publisher.snapshot().status, ObservationStatus::Unavailable);
        QVERIFY(!h.publisher.snapshot().cursors && !h.publisher.snapshot().metadata);
        QCOMPARE(h.reads.size(), 1);
    }
    void phaseBackNeverRelabelsOldContent_data()
    {
        QTest::addColumn<bool>("published");
        QTest::newRow("published") << true;
        QTest::newRow("before-first-sample") << false;
    }
    void phaseBackNeverRelabelsOldContent()
    {
        QFETCH(bool, published);
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}}));
        h.lyrics(lyric());
        if (published)
            QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        const auto old = h.publisher.snapshot();
        h.session(SessionPhase::Authenticating);
        h.session(SessionPhase::Guest); // same token/account, before queued capture
        QTest::qWait(1);
        QCOMPARE(h.publisher.snapshot().status, ObservationStatus::Unavailable);
        QVERIFY(!h.publisher.document(old.binding, old.lyricsRevision));
        QVERIFY(h.owner.queue().active.isNull());              // a phase change resets the queue
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}})); // explicit fresh load only
        QTRY_VERIFY(h.publisher.snapshot().song.has_value());
        QVERIFY(h.publisher.snapshot().binding != old.binding);
        QCOMPARE(h.publisher.snapshot().lyricsState, LyricsState::Loading);
    }
    void cursorBudgetAndSampleClockAreIndependent()
    {
        Harness h;
        auto l = lyric();
        l.original.enhanced.lines[0].words = QList<LyricWord>(19000, LyricWord{0, 5000, "x"});
        l.translation.enhanced = l.original.enhanced;
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}}));
        h.state(PlaybackState::Playing, 1000);
        h.lyrics(l);
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        auto a = h.publisher.snapshot();
        QCOMPARE(a.cursorStatus, ObservationContentStatus::LimitExceeded);
        QVERIFY(!a.cursors);
        QVERIFY(h.publisher.document(a.binding, a.lyricsRevision));
        QTest::qWait(2);
        QVERIFY(h.owner.setLyricOffset(1));
        QTRY_VERIFY(h.publisher.snapshot().sequence > a.sequence);
        QCOMPARE(h.publisher.snapshot().positionObservedMonoMs, a.positionObservedMonoMs);
        QVERIFY(h.publisher.snapshot().sampleMonoMs > a.sampleMonoMs);
    }
    void boundedFakeSinkAndFourHz()
    {
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}}));
        h.state(PlaybackState::Playing, 1);
        QTRY_COMPARE(h.publisher.snapshot().state, PlaybackState::Playing);
        QObject sink;
        bool wake = false;
        int wakes = 0, events = 0;
        PlaybackObservation last;
        connect(
            &h.publisher, &PlaybackObservationPublisher::changed, &sink,
            [&](PlaybackObservation s) {
                ++events;
                last = s; // one latest-value mailbox, no queued payload copies
                if (!wake) {
                    wake = true;
                    QTimer::singleShot(0, &sink, [&] {
                        wake = false;
                        ++wakes;
                        last = h.publisher.snapshot(); // recheck privacy at consumption
                    });
                }
            },
            Qt::DirectConnection);
        for (int i = 2; i <= 1000; ++i)
            h.state(PlaybackState::Playing, i);
        QCOMPARE(events, 0);
        QTRY_COMPARE(h.publisher.snapshot().positionMs, std::optional<qint64>(1000));
        QTRY_COMPARE(wakes, 1);
        QCOMPARE(events, 1);
        h.session(SessionPhase::Authenticating);
        QTRY_COMPARE(last.status, ObservationStatus::Unavailable);
        QVERIFY(!last.song && !last.cursors);
        QCOMPARE(h.reads.size(), 1);
        QCOMPARE(h.commands.size(), 1);
    }
    void boundedDocument_data()
    {
        QTest::addColumn<int>("mode");
        for (int n = 0; n < 10; ++n)
            QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void boundedDocument()
    {
        QFETCH(int, mode);
        Harness h;
        auto l = lyric();
        auto& t = l.original.standard;
        if (mode == 0)
            t.lines = {LyricLine{
                {}, {}, QString(PlaybackObservationPublisher::MaximumTextUnits + 1, u'x'), {}}};
        if (mode == 1)
            t.lines = QList<LyricLine>(10001);
        if (mode == 2)
            t.lines[0].words = QList<LyricWord>(30001);
        if (mode == 3)
            t.lines[0].text = QString(QChar(0xd800));
        if (mode == 4)
            t.lines[0].text = QString(QChar::Null);
        if (mode == 5)
            t.providerOffsetMs = ObservedPlayback::MaximumMs + 1;
        if (mode == 6)
            t.lines[0].startMs = -1;
        if (mode == 7)
            t.lines[0].words = {{ObservedPlayback::MaximumMs, 1, "x"}};
        if (mode == 8)
            t.availability = static_cast<LyricAvailability>(99);
        if (mode == 9)
            t.timing = static_cast<LyricTiming>(99);
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}}));
        h.lyrics(l);
        QTRY_COMPARE(h.publisher.snapshot().lyricsState, LyricsState::Ready);
        const auto p = h.publisher.snapshot();
        QCOMPARE(p.documentStatus, mode < 3 ? ObservationContentStatus::LimitExceeded
                                            : ObservationContentStatus::InvalidData);
        QVERIFY(!h.publisher.document(p.binding, p.lyricsRevision));
        QVERIFY(!p.cursors);
        QCOMPARE(h.owner.lyricsState(), LyricsState::Ready); // never mutate source facts
    }
    void missingAndFailureAreNotEmptyDocuments()
    {
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"9007199254740993"}}));
        SongLyrics empty;
        empty.song = {"9007199254740993"};
        h.lyrics(empty);
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        const auto p = h.publisher.snapshot();
        const auto d = h.publisher.document(p.binding, p.lyricsRevision);
        QVERIFY(d);
        QCOMPARE(d->lyrics.original.standard.availability, LyricAvailability::Missing);
        QVERIFY(!d->lyrics.instrumental && !d->lyrics.providerNoLyric);
        QVERIFY(h.owner.replaceQueue({{"2"}}));
        h.selectSource();
        QVERIFY(!h.reads.isEmpty());
        const auto r = h.reads.last();
        h.facade.deliverMusic({r.id, r.generation, r.context, MusicFailure{MusicError::Network}});
        QTRY_COMPARE(h.publisher.snapshot().lyricsState, LyricsState::Failed);
        QCOMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::NotReady);
    }
    void boundedBridgeAndStaleCommands()
    {
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"1"}, {"2"}}));
        h.state(PlaybackState::Playing);
        QTRY_VERIFY(h.publisher.snapshot().capabilities.seek);
        const auto a = h.publisher.snapshot();
        QSignalSpy finished(&h.bridge, &SystemMediaCommands::finished);
        SystemMediaCommand c;
        c.action = SystemMediaAction::Volume;
        c.volume = 0.7f;
        for (int n = 0; n < 8; ++n)
            QVERIFY(h.bridge.submit(a.binding, c));
        QVERIFY(!h.bridge.submit(a.binding, c));
        QTRY_COMPARE(finished.size(), 8);
        QCOMPARE(h.commands.size(), 10); // play, preload song 2 once it plays, 8 volumes
        QVERIFY(!h.publisher.snapshot().volumeLinear); // accepted commands are not feedback
        QVERIFY(h.bridge.submit(a.binding, c));
        QVERIFY(h.owner.next());
        QTRY_COMPARE(finished.size(), 9);
        QVERIFY(!finished.last()[1].toBool());
        QVERIFY(!h.bridge.submit(a.binding, c));
        QCOMPARE(h.commands.size(), 11);
        c.action = SystemMediaAction::Seek;
        c.positionMs = -1;
        QVERIFY(!h.bridge.submit(h.publisher.snapshot().binding, c));
    }
    void newerScalarIntentRejectsQueuedNativeCommand_data()
    {
        QTest::addColumn<int>("action");
        for (int i = 0; i < 5; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void newerScalarIntentRejectsQueuedNativeCommand()
    {
        QFETCH(int, action);
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"1"}}));
        h.state(PlaybackState::Playing);
        QTRY_VERIFY(h.publisher.snapshot().song.has_value());
        QSignalSpy result(&h.bridge, &SystemMediaCommands::finished);
        QVERIFY(
            h.bridge.submit(h.publisher.snapshot().binding, {SystemMediaAction::Volume, 0, 0.1f}));
        if (action == 0)
            QVERIFY(h.owner.pause());
        if (action == 1)
            QVERIFY(h.owner.resume());
        if (action == 2)
            QVERIFY(h.owner.seek(2000));
        if (action == 3)
            QVERIFY(h.owner.setVolume(0.9f));
        if (action == 4)
            QVERIFY(h.owner.setModes(QueueOrder::Sequential, QueueRepeat::All));
        const auto commands = h.commands.size();
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result.first()[1].toBool());
        QCOMPARE(h.commands.size(), commands);
    }
    void bridgeCloseAndReentrantPublication()
    {
        Harness h;
        QVERIFY(h.owner.replaceQueue({{"1"}, {"2"}}));
        QTRY_VERIFY(h.publisher.snapshot().song.has_value());
        const auto a = h.publisher.snapshot();
        QSignalSpy result(&h.bridge, &SystemMediaCommands::finished);
        QVERIFY(h.bridge.submit(a.binding, {SystemMediaAction::Next}));
        h.bridge.close();
        QCOMPARE(result.size(), 1);
        QVERIFY(!result.first()[1].toBool());
        QTest::qWait(1);
        QCOMPARE(h.commands.size(), 1);
        bool once = false;
        connect(&h.publisher, &PlaybackObservationPublisher::changed, &h.publisher,
                [&](PlaybackObservation s) {
                    if (!once && s.song && s.song->value == "1") {
                        once = true;
                        QVERIFY(h.owner.next());
                        QVERIFY(!h.publisher.document(s.binding, s.lyricsRevision));
                    }
                });
        h.state(PlaybackState::Paused);
        QTRY_VERIFY(h.publisher.snapshot().song && h.publisher.snapshot().song->value == "2");
        h.publisher.close();
        QCOMPARE(h.publisher.snapshot().status, ObservationStatus::Unavailable);
        QVERIFY(!h.bridge.submit(h.publisher.snapshot().binding, {SystemMediaAction::Play}));
    }
    void pureNavigationDoesNotConsumeRandomness()
    {
        int randoms = 0;
        PlaybackQueue q([&](quint32) {
            ++randoms;
            return 0;
        });
        QVERIFY(q.replace({{"1"}, {"2"}, {"3"}}));
        QVERIFY(q.setModes(QueueOrder::Shuffle, QueueRepeat::All));
        const auto before = randoms;
        for (int n = 0; n < 100; ++n) {
            QVERIFY(q.canNext());
            QVERIFY(!q.canPrevious());
        }
        QCOMPARE(randoms, before);
        QVERIFY(q.next());
        QVERIFY(q.next());
        QVERIFY(q.canPrevious());
        QVERIFY(q.previous());
        QVERIFY(q.canNext());
        QCOMPARE(randoms, before);
    }
    void feedbackNotAcknowledgement()
    {
        SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend media;
        PlaybackCoordinator coordinator(session, resolver, media);
        const LoadToken token{session.snapshot().token, 1};
        QVERIFY(coordinator.start(token, {{"1"}}));
        resolver.finish(0, StreamSource{{"1"},
                                        QUrl("https://m802.music.126.net/synthetic.mp3"),
                                        "standard",
                                        "mp3",
                                        100,
                                        QDeadlineTimer(60000)});
        using K = PlaybackEvent::Kind;
        emit media.event({token, K::Duration, {}, 10000});
        emit media.event({token, K::Seekable, {}, 1});
        emit media.event({token, K::Ready});
        QVERIFY(coordinator.seek(1000));
        coordinator.setVolume(0.4f);
        QCOMPARE(coordinator.snapshot().seekRevision, quint64(0));
        QVERIFY(!coordinator.snapshot().volumeLinear);
        emit media.event({token, K::Position, {}, 100});
        QCOMPARE(coordinator.snapshot().positionRevision, quint64(1));
        emit media.event({token, K::SeekConfirmed, {}, 1000});
        QCOMPARE(coordinator.snapshot().seekRevision, quint64(1));
        PlaybackEvent volume{token, K::Volume};
        volume.volumeLinear = 0.4f;
        emit media.event(volume);
        QCOMPARE(coordinator.snapshot().volumeLinear, std::optional<float>(0.4f));
        volume.volumeLinear = std::numeric_limits<float>::quiet_NaN();
        emit media.event(volume);
        QCOMPARE(coordinator.snapshot().volumeLinear, std::optional<float>(0.4f));
        volume.token.generation = 2;
        volume.volumeLinear = 0.8f;
        emit media.event(volume);
        emit media.event({{token.session, 2}, K::SeekConfirmed, {}, 5000});
        QCOMPARE(coordinator.snapshot().seekRevision, quint64(1));
        QCOMPARE(coordinator.snapshot().volumeLinear, std::optional<float>(0.4f));
        coordinator.stop();
        QVERIFY(!coordinator.snapshot().volumeLinear);
        emit media.event({token, K::SeekConfirmed, {}, 2000});
        QCOMPARE(coordinator.snapshot().seekRevision, quint64(0));
    }
};
QTEST_GUILESS_MAIN(PlaybackObservationTest)
#include "playback_observation_test.moc"
