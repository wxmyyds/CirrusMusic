#include "application/playback/listening_session.h"
#include "application/playback/vocal_spans.h"
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace cirrus::application;
using namespace cirrus::domain;
namespace
{
struct Harness {
    BackendFacade facade;
    ListeningSession player{facade};
    QList<PlaybackCommand> commands;
    QList<MusicCommand> music;
    bool acknowledge = true;
    Harness()
    {
        QObject::connect(&facade, &BackendFacade::playbackRequested, &facade,
                         [this](PlaybackCommand c) {
                             commands.append(c);
                             if (acknowledge)
                                 QTimer::singleShot(0, &facade, [this, c] {
                                     BackendEvent e;
                                     e.kind = BackendEvent::Kind::Acknowledged;
                                     e.commandId = c.id;
                                     e.accepted = true;
                                     facade.deliver(e);
                                 });
                         });
        QObject::connect(&facade, &BackendFacade::musicRequested, &facade,
                         [this](MusicCommand c) { music.append(c); });
        facade.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        facade.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest, {}};
        facade.deliver(e);
    }
    PlaybackCommand latestPlay() const
    {
        for (auto i = commands.crbegin(); i != commands.crend(); ++i)
            if (i->action == PlaybackAction::Play)
                return *i;
        return {};
    }
    int plays() const
    {
        int n = 0;
        for (const auto& c : commands)
            if (c.action == PlaybackAction::Play)
                ++n;
        return n;
    }
    void selectSource(SourceProvider provider = SourceProvider::Netease)
    {
        const auto c = latestPlay();
        facade.deliverPlaybackSource({c.token, c.query.song, provider,
            provider == SourceProvider::Bilibili
                ? std::optional<BiliStreamMeta>(BiliStreamMeta{"BV1testpage", "456", "30280", {}})
                : std::nullopt});
    }
    void publish(PlaybackState state, qint64 position = 0, std::optional<PreviewRange> preview = {})
    {
        const auto c = latestPlay();
        if (state != PlaybackState::Failed && state != PlaybackState::Resolving)
            selectSource();
        PlaybackSnapshot s;
        s.token = c.token;
        s.song = c.query.song;
        s.state = state;
        s.intent = c.intent;
        s.positionMs = position;
        s.durationMs = 10000;
        s.seekable = true;
        s.preview = preview;
        if (state == PlaybackState::Failed)
            s.failure = PlaybackFailure{PlaybackError::Unavailable};
        facade.deliverPlayback({c.generation, s});
    }
    void deliverLyrics(const MusicCommand& c, SongLyrics lyrics)
    {
        facade.deliverMusic({c.id, c.generation, c.context, std::move(lyrics)});
    }
};
LyricTrack lines(std::initializer_list<qint64> times)
{
    LyricTrack t;
    t.availability = LyricAvailability::Present;
    t.timing = LyricTiming::Lines;
    for (const auto time : times)
        t.lines.append({time, std::nullopt, "synthetic", {}});
    return t;
}
SongLyrics lyrics(QString id = "1")
{
    SongLyrics l;
    l.song = {id};
    l.original.standard = lines({0, 1000, 3000});
    l.translation.standard = lines({1500});
    l.romanization.enhanced = lines({500, 2500});
    return l;
}
} // namespace
class ListeningTest : public QObject
{
    Q_OBJECT
  private slots:
    void lyricsWaitForSelectedProvider()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}}));
        QVERIFY(h.music.isEmpty());
        QCOMPARE(h.player.lyricsState(), LyricsState::Loading);
        h.selectSource(SourceProvider::Bilibili);
        QCOMPARE(h.music.size(), 1);
        const auto query = std::get<PlaybackLyricQuery>(h.music.first().query);
        QCOMPARE(query.source.provider, SourceProvider::Bilibili);
        QCOMPARE(query.source.bili->cid, QString("456"));
        h.facade.deliverMusic({h.music.first().id, h.music.first().generation,
                              h.music.first().context, MusicFailure{MusicError::Network}});
        QCOMPARE(h.player.lyricsState(), LyricsState::Failed);
        h.selectSource(SourceProvider::Bilibili);
        QCOMPARE(h.music.size(), 1);
    }
    void stagedQueueIsBoundToSessionBeforeFirstPlay()
    {
        Harness h;
        QVERIFY(h.player.append({{"1"}}));
        const auto old = h.player.queue().entries.first().id;
        QSignalSpy commands(&h.facade, &BackendFacade::commandRequested);
        QVERIFY(h.facade.signOut());
        QVERIFY(h.player.queue().entries.isEmpty()); // before the deferred invalidation signal
        QVERIFY(!h.player.select(old));
        QVERIFY(!h.player.resume());
        BackendEvent e;
        e.kind = BackendEvent::Kind::Session;
        e.generation = qvariant_cast<BackendCommand>(commands.last().first()).generation;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest, {}};
        h.facade.deliver(e);
        QVERIFY(h.player.queue().entries.isEmpty());
        QVERIFY(h.player.append({{"2"}}));
        QCOMPARE(h.player.queue().entries.size(), 1);
        QVERIFY(!h.player.select(old));
        h.facade.close();
        QVERIFY(!h.player.append({{"3"}}));
    }
    void queueDuplicatesAndInsertionOrder()
    {
        PlaybackQueue q;
        QVERIFY(q.replace({{"1"}, {"1"}, {"2"}}));
        auto s = q.snapshot();
        QVERIFY(s.entries[0].id != s.entries[1].id);
        QCOMPARE(s.entries[0].song, s.entries[1].song);
        QVERIFY(q.select(s.entries[0].id));
        QVERIFY(q.append({{"3"}}, true));
        QVERIFY(q.append({{"4"}}, true));
        QVERIFY(q.next());
        QCOMPARE(q.current()->song.value, "3");
        QVERIFY(q.next());
        QCOMPARE(q.current()->song.value, "4");
        QVERIFY(q.next());
        QCOMPARE(q.current()->id, s.entries[1].id);
        QVERIFY(q.previous());
        QCOMPARE(q.current()->song.value, "4");
        QVERIFY(!q.replace({{"0"}}));
        QCOMPARE(q.current()->song.value, "4");
    }
    void insertedNextOverridesForwardHistory()
    {
        PlaybackQueue q;
        QVERIFY(q.replace({{"1"}, {"2"}, {"3"}}));
        QVERIFY(q.next());
        QVERIFY(q.next());
        QVERIFY(q.previous());
        QVERIFY(q.append({{"4"}}, true));
        QVERIFY(q.append({{"5"}}, true));
        QVERIFY(q.next());
        QCOMPARE(q.current()->song.value, "4");
        QVERIFY(q.next());
        QCOMPARE(q.current()->song.value, "5");
        QVERIFY(q.next());
        QCOMPARE(q.current()->song.value, "2");
        QVERIFY(q.next());
        QCOMPARE(q.current()->song.value, "3");
        QVERIFY(q.setModes(QueueOrder::Sequential, QueueRepeat::One));
        QVERIFY(q.next(false));
        QCOMPARE(q.current()->song.value, "1");
    }
    void lyricsFailureDoesNotInterruptAudioAndSingleRepeatIsBounded()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}}));
        h.publish(PlaybackState::Playing, 1000);
        const auto request = h.music.last();
        h.facade.deliverMusic(
            {request.id, request.generation, request.context, MusicFailure{MusicError::Network}});
        QCOMPARE(h.player.lyricsState(), LyricsState::Failed);
        QCOMPARE(h.player.playback().state, PlaybackState::Playing);
        QVERIFY(h.player.setModes(QueueOrder::Sequential, QueueRepeat::One));
        h.publish(PlaybackState::Ended);
        QTRY_COMPARE(h.plays(), 2);
        QSignalSpy stopped(&h.player, &ListeningSession::advanceStopped);
        h.publish(PlaybackState::Failed);
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(h.plays(), 2);
        QVERIFY(stopped.first()[0].toBool());
    }
    void shuffleIsOneRoundAndHistoryIsStable()
    {
        PlaybackQueue q([](quint32) { return 0; });
        QVERIFY(q.setModes(QueueOrder::Shuffle, QueueRepeat::Off));
        QVERIFY(q.replace({{"1"}, {"2"}, {"3"}, {"4"}}));
        const auto first = q.snapshot().entries[2].id;
        QVERIFY(q.select(first));
        QSet<QUuid> seen{first};
        for (int i = 0; i < 3; ++i) {
            QVERIFY(q.next());
            QVERIFY(!seen.contains(q.current()->id));
            seen.insert(q.current()->id);
        }
        QCOMPARE(seen.size(), 4);
        QVERIFY(!q.next());
        const auto last = q.current()->id;
        QVERIFY(q.previous());
        const auto previous = q.current()->id;
        QVERIFY(previous != last);
        QVERIFY(q.next());
        QCOMPARE(q.current()->id, last);
        QVERIFY(q.setModes(QueueOrder::Shuffle, QueueRepeat::All));
        QVERIFY(q.next());
        QVERIFY(q.current()->id != last);
        QVERIFY(q.setModes(QueueOrder::Shuffle, QueueRepeat::One));
        const auto current = q.current()->id;
        QVERIFY(q.next(true));
        QCOMPARE(q.current()->id, current);
        QVERIFY(q.next(false));
        QVERIFY(q.current()->id != current);
    }
    void moveFollowsPlayOrder()
    {
        PlaybackQueue q([](quint32) { return 0; });
        QVERIFY(q.replace({{"1"}, {"2"}, {"3"}, {"4"}}));
        const auto s = q.snapshot();
        QVERIFY(q.select(s.entries[1].id));
        QVERIFY(q.move(s.entries[3].id, 0));
        QCOMPARE(q.playOrder(), (QList<QUuid>{s.entries[3].id, s.entries[0].id, s.entries[1].id,
                                              s.entries[2].id}));
        QCOMPARE(q.snapshot().entries[0].id, s.entries[3].id); // in order, entries follow
        QCOMPARE(q.current()->id, s.entries[1].id);
        QVERIFY(q.next());
        QCOMPARE(q.current()->song.value, "3");
        QVERIFY(!q.move(s.entries[0].id, 4));
        QVERIFY(!q.move(QUuid::createUuid(), 0));
        // Shuffled, only the play order changes.
        QVERIFY(q.setModes(QueueOrder::Shuffle, QueueRepeat::Off));
        const auto entries = q.snapshot().entries;
        const auto order = q.playOrder();
        QVERIFY(q.move(order.last(), 1));
        QCOMPARE(q.playOrder().at(1), order.last());
        QCOMPARE(q.snapshot().entries.size(), entries.size());
        for (qsizetype i = 0; i < entries.size(); ++i)
            QCOMPARE(q.snapshot().entries[i].id, entries[i].id);
    }
    void queueRemovalAndLimits()
    {
        PlaybackQueue q;
        QVERIFY(q.replace({{"1"}, {"2"}, {"3"}}));
        const auto s = q.snapshot();
        QVERIFY(q.select(s.entries[0].id));
        QCOMPARE(q.successor(), s.entries[1].id);
        QVERIFY(q.remove(s.entries[1].id));
        QCOMPARE(q.successor(), s.entries[2].id);
        QVERIFY(q.next());
        QVERIFY(q.previous());
        QCOMPARE(q.current()->song.value, "1");
        QVERIFY(q.remove(s.entries[0].id));
        QVERIFY(!q.current());
        QList<SongId> huge(PlaybackQueue::MaximumEntries + 1, SongId{"1"});
        QVERIFY(!q.replace(huge));
        q.clear();
        QVERIFY(!q.next());
        QVERIFY(!q.previous());
    }
    void independentTracksOffsetsAndOverlaps()
    {
        auto doc = lyrics();
        doc.original.standard = lines({1000, 1000, 3000});
        doc.translation.standard.providerOffsetMs = 100;
        auto& word = doc.original.enhanced;
        word.availability = LyricAvailability::Present;
        word.timing = LyricTiming::Words;
        word.lines = {{1000, 4000, "ab", {{1000, 1800, "a"}, {2200, 1200, "b"}}},
                      {2300, 2600, "c", {{2300, 100, "c"}}}};
        LyricTimeline timeline;
        timeline.reset(doc);
        QVERIFY(timeline.at(999).original.standard.lines.isEmpty());
        QCOMPARE(timeline.at(1000).original.standard.lines, (QList<qsizetype>{0, 1}));
        QCOMPARE(timeline.at(3000).original.standard.lines, (QList<qsizetype>{2}));
        QVERIFY(timeline.at(1599).translation.standard.lines.isEmpty());
        QCOMPARE(timeline.at(1600).translation.standard.lines, (QList<qsizetype>{0}));
        auto c = timeline.at(2350);
        QCOMPARE(c.original.enhanced.lines, (QList<qsizetype>{0, 1}));
        QCOMPARE(c.original.enhanced.words, (QList<ActiveLyricWord>{{0, 0}, {0, 1}, {1, 0}}));
        QCOMPARE(timeline.at(2400).original.enhanced.words.size(), 2); // exclusive word end
        QVERIFY(timeline.at(4000).original.enhanced.lines.isEmpty());
        QVERIFY(c.translation.enhanced.lines.isEmpty());
        QVERIFY(c.romanization.standard.lines.isEmpty());
        QCOMPARE(timeline.at(999, -1).original.standard.lines, (QList<qsizetype>{0, 1}));
        QVERIFY(timeline.at(1000, 1).original.standard.lines.isEmpty());
        timeline.clear();
        QVERIFY(timeline.at(99999).original.standard.lines.isEmpty());
    }
    void vocalSpansComeFromTheTimeline()
    {
        // Lines: until the next line (a blank break counts), 8 s at most; credits and
        // blank lines aren't sung; the provider's and the user's offsets move them.
        auto doc = lyrics();
        auto& standard = doc.original.standard;
        standard = lines({});
        standard.lines = {{0, std::nullopt, "作词 : 某人", {}},
                          {12000, std::nullopt, "first line", {}},
                          {15000, std::nullopt, "second line", {}},
                          {18000, std::nullopt, "", {}},
                          {40000, std::nullopt, "last line", {}},
                          {9000, std::nullopt, "early, out of order", {}}};
        standard.providerOffsetMs = 100;
        QCOMPARE(vocalSpans(doc, 50),
                 (QList<VocalSpan>{{9150, 12150}, {12150, 15150}, {15150, 18150}, {40150, 48150}}));
        // Word timing wins: from the first word's start to the last one's end.
        auto& enhanced = doc.original.enhanced;
        enhanced.availability = LyricAvailability::Present;
        enhanced.timing = LyricTiming::Words;
        enhanced.lines = {{1000, 4000, "ab", {{1000, 800, "a"}, {2200, 1200, "b"}}}};
        QCOMPARE(vocalSpans(doc), (QList<VocalSpan>{{1000, 3400}}));
        // Instrumental, or nothing timed: no singing known.
        doc.instrumental = true;
        QVERIFY(vocalSpans(doc).isEmpty());
        SongLyrics plain;
        plain.original.standard.availability = LyricAvailability::Present;
        QVERIFY(vocalSpans(plain).isEmpty());
    }
    void textOnlyMalformedAndBackwardSeek()
    {
        auto doc = lyrics();
        doc.translation.standard.availability = LyricAvailability::Malformed;
        doc.romanization.enhanced.timing = LyricTiming::PlainText;
        LyricTimeline t;
        t.reset(doc);
        QCOMPARE(t.at(3500).original.standard.lines, (QList<qsizetype>{2}));
        QCOMPARE(t.at(500).original.standard.lines, (QList<qsizetype>{0}));
        QVERIFY(t.at(5000).translation.standard.lines.isEmpty());
        QVERIFY(t.at(5000).romanization.enhanced.lines.isEmpty());
        QVERIFY(t.at(-1).original.standard.lines.isEmpty());
    }
    void lyricsFollowMediaPauseSeekAndPreview()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}, {"2"}}));
        h.selectSource();
        auto doc = lyrics();
        doc.original.standard = lines({0, 30000, 31500});
        h.deliverLyrics(h.music.last(), doc);
        QCOMPARE(h.player.lyricsState(), LyricsState::Ready);
        QVERIFY(h.player.cursors().original.standard.lines.isEmpty()); // still resolving
        h.publish(PlaybackState::Playing, 1000, PreviewRange{30000, 40000});
        QCOMPARE(h.player.cursors().original.standard.lines, (QList<qsizetype>{1}));
        h.publish(PlaybackState::Paused, 2000, PreviewRange{30000, 40000});
        QCOMPARE(h.player.cursors().original.standard.lines, (QList<qsizetype>{2}));
        const auto cursor = h.player.cursors();
        QCoreApplication::processEvents();
        QCOMPARE(h.player.cursors(), cursor);
        QVERIFY(h.player.seek(0));
        QCOMPARE(h.commands.last().positionMs, 0);
        h.publish(PlaybackState::Paused, 0, PreviewRange{30000, 40000});
        QCOMPARE(h.player.cursors().original.standard.lines, (QList<qsizetype>{1}));
        QVERIFY(h.player.setLyricOffset(1));
        QCOMPARE(h.player.cursors().original.standard.lines, (QList<qsizetype>{0}));
        QVERIFY(!h.player.setLyricOffset(120001));
        h.publish(PlaybackState::Paused, Q_INT64_C(9223372036854775807),
                  PreviewRange{30000, 40000});
        QVERIFY(h.player.cursors().original.standard.lines.isEmpty()); // reject before addition
        h.publish(PlaybackState::Paused, -1, PreviewRange{30000, 40000});
        QVERIFY(h.player.cursors().original.standard.lines.isEmpty());
    }
    void lateLyricsAndMediaCannotReplaceSelection()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}, {"2"}}));
        h.selectSource();
        const auto old = h.music.last();
        const auto command = h.latestPlay();
        QVERIFY(h.player.next());
        h.deliverLyrics(old, lyrics("1"));
        QVERIFY(!h.player.lyrics());
        h.selectSource();
        h.deliverLyrics(h.music.last(), lyrics("2"));
        QCOMPARE(h.player.lyrics()->song.value, "2");
        PlaybackSnapshot oldPlayback;
        oldPlayback.token = command.token;
        oldPlayback.song = {"1"};
        oldPlayback.state = PlaybackState::Ended;
        h.facade.deliverPlayback({command.generation, oldPlayback});
        QCoreApplication::processEvents();
        QCOMPARE(h.player.playback().song.value, "2");
        QVERIFY(h.facade.signOut());
        QCoreApplication::processEvents();
        QVERIFY(h.player.queue().entries.isEmpty());
        QVERIFY(!h.player.lyrics());
    }
    void pauseIsPreservedAcrossNextAndRemoval()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}, {"2"}, {"3"}}));
        QVERIFY(h.player.pause());
        QVERIFY(h.player.next());
        QCOMPARE(h.latestPlay().intent, TransportIntent::Pause);
        QCOMPARE(h.latestPlay().query.song.value, "2");
        const auto active = h.player.queue().active;
        QVERIFY(h.player.remove(active));
        QCOMPARE(h.latestPlay().intent, TransportIntent::Pause);
        QCOMPARE(h.latestPlay().query.song.value, "3");
        QVERIFY(h.player.remove(h.player.queue().active));
        QCOMPARE(h.player.playback().state, PlaybackState::Empty);
        QCOMPARE(h.player.queue().entries.size(), 1); // older entry remains, not silently wrapped
    }
    void terminalEventsAdvanceOnceAndRespectModes()
    {
        Harness h;
        QSignalSpy ended(&h.player, &ListeningSession::advanceStopped);
        QVERIFY(h.player.replaceQueue({{"1"}, {"2"}}));
        h.publish(PlaybackState::Ended);
        h.publish(PlaybackState::Ended);
        QTRY_COMPARE(h.plays(), 2);
        QCOMPARE(h.player.playback().song.value, "2");
        h.publish(PlaybackState::Ended);
        QTRY_COMPARE(ended.size(), 1);
        QCOMPARE(ended.last()[0].toBool(), false);
        QCOMPARE(h.plays(), 2);
        QVERIFY(h.player.setModes(QueueOrder::Sequential, QueueRepeat::All));
        QVERIFY(h.player.resume());
        h.publish(PlaybackState::Ended);
        QTRY_COMPARE(h.player.playback().song.value, "1");
        QVERIFY(h.player.setModes(QueueOrder::Sequential, QueueRepeat::One));
        const int count = h.plays();
        h.publish(PlaybackState::Ended);
        QTRY_COMPARE(h.plays(), count + 1);
        QCOMPARE(h.player.playback().song.value, "1");
        QVERIFY(h.player.next());
        QCOMPARE(h.player.playback().song.value, "2");
    }
    void failuresAdvanceAndStopAtTen()
    {
        Harness h;
        QList<SongId> songs;
        for (int i = 1; i <= 12; ++i)
            songs.append({QString::number(i)});
        QVERIFY(h.player.replaceQueue(songs));
        QSignalSpy stopped(&h.player, &ListeningSession::failureStreakExceeded);
        for (int i = 1; i <= 10; ++i) {
            h.publish(PlaybackState::Ready);
            h.publish(PlaybackState::Failed);
            h.publish(PlaybackState::Failed);
            if (i < 10)
                QTRY_COMPARE(h.plays(), i + 1);
        }
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(h.plays(), 10);
        QCOMPARE(h.player.playback().state, PlaybackState::Empty);
        QCOMPARE(h.player.queue().entries.size(), 12);
        QCoreApplication::processEvents();
        QCOMPARE(stopped.size(), 1);
    }

    void resolverFailureSettlesLyricsWithoutQuery()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}}));
        h.publish(PlaybackState::Failed);
        QCOMPARE(h.player.lyricsState(), LyricsState::Failed);
        QVERIFY(h.music.isEmpty());
        h.selectSource(SourceProvider::Bilibili);
        QVERIFY(h.music.isEmpty());
    }
    void failuresCountBeforeReentrantNext()
    {
        Harness h;
        QList<SongId> songs;
        for (int i = 1; i <= 12; ++i)
            songs.append({QString::number(i)});
        QVERIFY(h.player.replaceQueue(songs));
        const auto connection = connect(&h.player, &ListeningSession::playbackChanged, this,
                                         [&](PlaybackSnapshot s) {
            if (s.state == PlaybackState::Failed)
                QVERIFY(h.player.next());
        });
        for (int i = 0; i < 9; ++i)
            h.publish(PlaybackState::Failed);
        QCOMPARE(h.plays(), 10);
        disconnect(connection);
        QSignalSpy stopped(&h.player, &ListeningSession::failureStreakExceeded);
        h.publish(PlaybackState::Failed);
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(h.plays(), 10);
    }
    void wrongSongObservationCannotResetFailureStreak()
    {
        Harness h;
        QList<SongId> songs;
        for (int i = 1; i <= 12; ++i)
            songs.append({QString::number(i)});
        QVERIFY(h.player.replaceQueue(songs));
        for (int i = 0; i < 9; ++i) {
            h.publish(PlaybackState::Failed);
            QTRY_COMPARE(h.plays(), i + 2);
        }
        auto wrong = h.facade.playback();
        wrong.song = {"99"};
        wrong.state = PlaybackState::Playing;
        wrong.observed = ObservedPlayback{1000};
        h.facade.deliverPlayback({h.latestPlay().generation, wrong});
        QSignalSpy stopped(&h.player, &ListeningSession::failureStreakExceeded);
        h.publish(PlaybackState::Failed);
        QTRY_COMPARE(stopped.size(), 1);
    }
    void supersededStopDoesNotNotifyForNewPlayback()
    {
        Harness h;
        QList<SongId> songs;
        for (int i = 1; i <= 12; ++i)
            songs.append({QString::number(i)});
        QVERIFY(h.player.replaceQueue(songs));
        for (int i = 0; i < 9; ++i) {
            h.publish(PlaybackState::Failed);
            QTRY_COMPARE(h.plays(), i + 2);
        }
        connect(&h.player, &ListeningSession::playbackChanged, this, [&](PlaybackSnapshot s) {
            if (s.state == PlaybackState::Empty)
                QVERIFY(h.player.replaceQueue({{"99"}}));
        });
        QSignalSpy stopped(&h.player, &ListeningSession::failureStreakExceeded);
        h.publish(PlaybackState::Failed);
        QTRY_COMPARE(h.plays(), 11);
        QCOMPARE(stopped.size(), 0);
        QCOMPARE(h.player.playback().song.value, QString("99"));
    }
    void rejectedThresholdStopWaitsForMailboxSpace()
    {
        Harness h;
        QList<SongId> songs;
        for (int i = 1; i <= 12; ++i)
            songs.append({QString::number(i)});
        QVERIFY(h.player.replaceQueue(songs));
        for (int i = 0; i < 9; ++i) {
            h.publish(PlaybackState::Failed);
            QTRY_COMPARE(h.plays(), i + 2);
        }
        QCoreApplication::processEvents();
        h.acknowledge = false;
        QList<quint64> held;
        for (int i = 0; i < 32; ++i)
            held.append(h.facade.setPlaybackVolume(0));
        QVERIFY(held.last());
        QSignalSpy stopped(&h.player, &ListeningSession::failureStreakExceeded);
        h.publish(PlaybackState::Failed);
        QCoreApplication::processEvents();
        QCOMPARE(stopped.size(), 0);
        QCOMPARE(h.player.playback().state, PlaybackState::Failed);
        BackendEvent ack;
        ack.kind = BackendEvent::Kind::Acknowledged;
        ack.commandId = held.first();
        ack.accepted = true;
        h.facade.deliver(ack);
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(h.player.playback().state, PlaybackState::Empty);
        QCOMPARE(h.player.queue().entries.size(), 12);
    }
    void onlyObservedAudioResetsFailures()
    {
        Harness h;
        QList<SongId> songs;
        for (int i = 1; i <= 24; ++i)
            songs.append({QString::number(i)});
        QVERIFY(h.player.replaceQueue(songs));
        QSignalSpy stopped(&h.player, &ListeningSession::failureStreakExceeded);
        for (int i = 1; i <= 9; ++i) {
            h.publish(PlaybackState::Playing, 5000);
            h.publish(PlaybackState::Failed);
            QTRY_COMPARE(h.plays(), i + 1);
        }
        auto observed = h.facade.playback();
        observed.state = PlaybackState::Playing;
        observed.observed = ObservedPlayback{1000};
        h.facade.deliverPlayback({h.latestPlay().generation, observed});
        h.publish(PlaybackState::Failed);
        QTRY_COMPARE(h.plays(), 11);
        QCOMPARE(stopped.size(), 0);
        for (int i = 11; i <= 19; ++i) {
            h.publish(PlaybackState::Ready);
            h.publish(PlaybackState::Failed);
            if (i < 19)
                QTRY_COMPARE(h.plays(), i + 1);
        }
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(h.plays(), 19);
    }
    void stopKeepsQueueAndCloseClearsIt()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}, {"2"}}));
        const auto active = h.player.queue().active;
        h.publish(PlaybackState::Ended);
        QVERIFY(h.player.stop());
        QCoreApplication::processEvents();
        QCOMPARE(h.plays(), 1);
        QCOMPARE(h.player.queue().active, active);
        QVERIFY(!h.player.lyrics());
        QVERIFY(h.player.resume());
        QCOMPARE(h.plays(), 2);
        h.facade.close();
        QVERIFY(h.player.queue().entries.isEmpty());
        QVERIFY(h.player.cursors().original.standard.lines.isEmpty());
    }
    void previousRestartsAfterThreeSeconds()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}, {"2"}}));
        QVERIFY(h.player.next());
        h.publish(PlaybackState::Playing, 3001);
        QVERIFY(h.player.previous());
        QCOMPARE(h.commands.last().action, PlaybackAction::Seek);
        QCOMPARE(h.commands.last().positionMs, 0);
        h.publish(PlaybackState::Playing, 3000);
        QVERIFY(h.player.previous());
        QCOMPARE(h.latestPlay().query.song.value, "1");
    }
    void rejectedCommandDoesNotCommitQueue()
    {
        Harness h;
        h.acknowledge = false;
        QVERIFY(h.player.replaceQueue({{"1"}}));
        const auto id = h.player.queue().active;
        for (int i = 0; i < 31; ++i)
            QVERIFY(h.facade.poll());
        QVERIFY(!h.player.replaceQueue({{"2"}}));
        QCOMPARE(h.player.queue().active, id);
        QCOMPARE(h.player.playback().song.value, "1");
    }
    void observerSelectionWinsOverOuterClear()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}}));
        bool once = false;
        connect(&h.player, &ListeningSession::playbackChanged, &h.player, [&](PlaybackSnapshot s) {
            if (s.state == PlaybackState::Empty && !once) {
                once = true;
                QVERIFY(h.player.replaceQueue({{"9"}}));
            }
        });
        QVERIFY(h.player.clear());
        QCOMPARE(h.player.playback().song.value, "9");
        QCOMPARE(h.player.queue().entries.size(), 1);
    }
    void observerSelectionWinsOverOuterRemove()
    {
        Harness h;
        QVERIFY(h.player.replaceQueue({{"1"}}));
        const auto id = h.player.queue().active;
        bool once = false;
        connect(&h.player, &ListeningSession::playbackChanged, &h.player, [&](PlaybackSnapshot s) {
            if (s.state == PlaybackState::Empty && !once) {
                once = true;
                QVERIFY(h.player.replaceQueue({{"9"}}));
            }
        });
        QVERIFY(h.player.remove(id));
        QCOMPARE(h.player.playback().song.value, "9");
        QCOMPARE(h.player.queue().entries[0].song.value, "9");
    }
};
QTEST_GUILESS_MAIN(ListeningTest)
#include "listening_test.moc"
