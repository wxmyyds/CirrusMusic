#include "../support/fake_playback.h"
#include "application/playback/playback_coordinator.h"
#include <QtTest>
using namespace cirrus;
using namespace domain;
using Kind = application::PlaybackEvent::Kind;
namespace
{
StreamSource source(QString id)
{
    return {{id},       QUrl("https://m802.music.126.net/synthetic.mp3"),
            "standard", "mp3",
            100,        QDeadlineTimer(60000)};
}
} // namespace
class PlaybackTest : public QObject
{
    Q_OBJECT
  private slots:
    void selectionObserverCanRetireLoadAndPreload()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const auto context = session.snapshot().token;
        const LoadToken first{context, 1};
        const auto connection = connect(&player, &application::PlaybackCoordinator::sourceSelected,
                                        &player, [&] { player.stop({context, 2}); });
        QVERIFY(player.start(first, {{"1"}}));
        resolver.finish(0, source("1"));
        QVERIFY(backend.loads.isEmpty());
        disconnect(connection);
        const LoadToken third{context, 3}, fourth{context, 4};
        QVERIFY(player.start(third, {{"1"}}));
        resolver.finish(1, source("1"));
        QVERIFY(player.preload(third, fourth, StreamQuery{{"2"}}));
        connect(&player, &application::PlaybackCoordinator::sourceSelected, &player,
                [&] { QVERIFY(player.preload(third, {}, std::nullopt)); });
        resolver.finish(2, source("2"));
        QVERIFY(!backend.preloads.contains(fourth));
        QCOMPARE(player.snapshot().token, third);
    }
    void lyricEligibilityEndsWhenCurrentOrPreloadRetires()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const LoadToken first{session.snapshot().token, 1}, next{first.session, 2};
        QVERIFY(player.start(first, {{"1"}}));
        resolver.finish(0, source("1"));
        QVERIFY(player.acceptsLyrics(first));
        QVERIFY(player.preload(first, next, StreamQuery{{"2"}}));
        QVERIFY(!player.acceptsLyrics(next));
        resolver.finish(1, source("2"));
        QVERIFY(player.acceptsLyrics(next));
        emit backend.event({first, Kind::Failure, {}, 0, {PlaybackError::Decode}});
        QVERIFY(!player.acceptsLyrics(first));
        QVERIFY(!player.acceptsLyrics(next));
    }
    void initialPauseIsAtomic()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const LoadToken token{session.snapshot().token, 1};
        QVERIFY(player.start(token, {{"1"}}, TransportIntent::Pause));
        resolver.finish(0, source("1"));
        emit backend.event({token, Kind::Ready});
        QCOMPARE(backend.plays, 0);
        QCOMPARE(player.snapshot().state, PlaybackState::Paused);
    }
    void pauseAndCoalescedSeek()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const LoadToken token{session.snapshot().token, 1};
        QVERIFY(player.start(token, {{"1"}}));
        QVERIFY(player.pause());
        QVERIFY(player.seek(10));
        QVERIFY(player.seek(8000));
        resolver.finish(0, source("1"));
        QCOMPARE(backend.loads.size(), 1);
        QCOMPARE(backend.plays, 0);
        emit backend.event({token, Kind::Duration, {}, 5000});
        emit backend.event({token, Kind::Seekable, {}, 1});
        emit backend.event({token, Kind::Ready});
        QCOMPARE(backend.plays, 0);
        QCOMPARE(backend.seeks, QList<qint64>{4999});
        QCOMPARE(player.snapshot().state, PlaybackState::Paused);
        QVERIFY(player.resume());
        QCOMPARE(backend.plays, 1);
        emit backend.event({token, Kind::State, PlaybackState::Playing});
        QCOMPARE(player.snapshot().state, PlaybackState::Playing);
        QVERIFY(player.pause());
        QCOMPARE(player.snapshot().state, PlaybackState::Playing); // acceptance isn't effect
        emit backend.event(
            {token, Kind::State, PlaybackState::Playing}); // stale transport intention
        QCOMPARE(player.snapshot().state, PlaybackState::Playing);
        emit backend.event({token, Kind::State, PlaybackState::Paused});
        QCOMPARE(player.snapshot().state, PlaybackState::Paused);
    }
    void outOfOrderSourcesAndRetiredMedia()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const auto context = session.snapshot().token;
        QVERIFY(player.start({context, 1}, {{"1"}}));
        resolver.finish(0, source("1")); // A was actually loaded
        QVERIFY(player.start({context, 2}, {{"2"}}));
        QVERIFY(player.start({context, 3}, {{"3"}}));
        resolver.finish(2, source("3"));
        resolver.finish(1, source("2"));
        resolver.finish(0, source("1"));
        QCOMPARE(backend.loads.size(), 2);
        QCOMPARE(backend.loads.last().generation, 3);
        emit backend.event({{context, 1}, Kind::Failure, {}, 0, {PlaybackError::Decode}});
        emit backend.event({{context, 1}, Kind::Ended});
        emit backend.event({{context, 1}, Kind::Position, {}, 333});
        QCOMPARE(player.snapshot().song.value, "3");
        QCOMPARE(player.snapshot().positionMs, 0);
        emit backend.event({{context, 3}, Kind::Ready});
        emit backend.event({{context, 3}, Kind::Ended});
        QCOMPARE(player.snapshot().state, PlaybackState::Ended);
        const auto stops = backend.stops;
        emit backend.event({{context, 3}, Kind::Ended});
        QCOMPARE(backend.stops, stops);
        QVERIFY(!player.resume()); // explicit play opens a new generation, no old-source replay
    }
    void stopAndSessionIsolation()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const LoadToken token{session.snapshot().token, 1};
        QVERIFY(player.start(token, {{"1"}}));
        player.stop();
        resolver.finish(0, source("1"));
        QVERIFY(backend.loads.isEmpty());
        QVERIFY(player.start({token.session, 2}, {{"2"}}));
        static_cast<void>(session.beginAuthentication());
        resolver.finish(1, source("2"));
        QVERIFY(backend.loads.isEmpty());
        QCOMPARE(player.snapshot().state, PlaybackState::Empty);
        QVERIFY(!player.start({token.session, 3}, {{"3"}}));
    }
    void preloadedSongTakesOverWhenTheBackendAdvances()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const auto context = session.snapshot().token;
        const LoadToken first{context, 1}, second{context, 2};
        QVERIFY(player.start(first, {{"1"}}));
        resolver.finish(0, source("1"));
        emit backend.event({first, Kind::Ready});
        QVERIFY(!player.preload({context, 9}, second, StreamQuery{{"2"}})); // not the current
        QVERIFY(player.preload(first, second, StreamQuery{{"2"}}));
        QCOMPARE(resolver.requests.size(), 2);
        QVERIFY(backend.preloads.isEmpty()); // only once resolved
        auto next = source("2");
        next.actualQuality = "exhigh";
        resolver.finish(1, next);
        QCOMPARE(backend.preloads, QList<LoadToken>{second});
        QCOMPARE(player.snapshot().token, first); // still the first until the backend gets there
        emit backend.event({second, Kind::Advanced});
        QCOMPARE(player.snapshot().token, second);
        QCOMPARE(player.snapshot().song.value, "2");
        QCOMPARE(player.snapshot().state, PlaybackState::Playing);
        QCOMPARE(player.snapshot().actualQuality, "exhigh");
        emit backend.event({second, Kind::Duration, {}, 4000});
        QCOMPARE(player.snapshot().durationMs, std::optional<qint64>(4000));
        emit backend.event({first, Kind::Position, {}, 10}); // the old load is gone
        QCOMPARE(player.snapshot().positionMs, 0);
        QVERIFY(!player.start({context, 2}, {{"3"}})); // its generation is used
        // A new preload replaces the old; a new start drops it.
        QVERIFY(player.preload(second, {context, 3}, StreamQuery{{"3"}}));
        QVERIFY(player.preload(second, {context, 4}, StreamQuery{{"4"}}));
        QCOMPARE(resolver.canceled.last(), resolver.requests[2].id);
        QVERIFY(player.start({context, 5}, {{"5"}}));
        QCOMPARE(resolver.canceled.last(), resolver.requests[3].id);
        emit backend.event({{context, 4}, Kind::Advanced});
        QCOMPARE(player.snapshot().token, (LoadToken{context, 5}));
    }
    // Sung lines that come before the address wait for their load: the backend knows a
    // load only once it has it.
    void vocalsWaitForTheirLoad()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const auto context = session.snapshot().token;
        const LoadToken first{context, 1}, second{context, 2};
        const QList<VocalSpan> lines{{1000, 2000}};
        QVERIFY(player.start(first, {{"1"}}));
        player.setVocals(first, lines);
        QVERIFY(backend.vocals.isEmpty());
        resolver.finish(0, source("1"));
        QCOMPARE(backend.loads, QList<LoadToken>{first});
        QCOMPARE(backend.vocals, QList<LoadToken>{first});
        emit backend.event({first, Kind::Ready});
        QVERIFY(player.preload(first, second, StreamQuery{{"2"}}));
        player.setVocals(second, lines);
        QCOMPARE(backend.vocals.size(), 1);
        resolver.finish(1, source("2"));
        QCOMPARE(backend.vocals, (QList<LoadToken>{first, second}));
        player.setVocals(second, lines); // it has that one: at once
        QCOMPARE(backend.vocals.size(), 3);
        // A new load drops what waited for the old one.
        QVERIFY(player.start({context, 3}, {{"3"}}));
        player.setVocals({context, 3}, lines);
        QVERIFY(player.start({context, 4}, {{"4"}}));
        resolver.finish(3, source("4"));
        QCOMPARE(backend.loads.last(), (LoadToken{context, 4}));
        QCOMPARE(backend.vocals.size(), 3);
    }
    void expiredAddressIsResolvedAgain()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const LoadToken token{session.snapshot().token, 1};
        QVERIFY(player.start(token, {{"1"}}));
        resolver.finish(0, source("1"));
        emit backend.event({token, Kind::Ready});
        emit backend.event({token, Kind::SourceExpired});
        emit backend.event({token, Kind::SourceExpired}); // already asking
        QCOMPARE(resolver.requests.size(), 2);
        QCOMPARE(resolver.requests[1].query.song.value, "1");
        resolver.finish(1, source("1"));
        QCOMPARE(backend.refreshes, QList<LoadToken>{token});
        QCOMPARE(player.snapshot().state, PlaybackState::Ready); // playback goes on
        emit backend.event({token, Kind::SourceExpired});
        resolver.finish(2, PlaybackFailure{PlaybackError::Unavailable});
        QCOMPARE(backend.refreshes.size(), 1); // the backend fails it as Expired itself
    }
    void permissionExpiryAndErrorAreBounded()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const auto context = session.snapshot().token;
        QVERIFY(player.start({context, 1}, {{"1"}}));
        resolver.finish(0, PlaybackFailure{PlaybackError::Unavailable, 404});
        QCOMPARE(player.snapshot().state, PlaybackState::Failed);
        QCOMPARE(resolver.requests.size(), 1);
        QVERIFY(backend.loads.isEmpty());
        QVERIFY(player.start({context, 2}, {{"2"}}));
        auto expired = source("2");
        expired.validUntil = QDeadlineTimer(0);
        resolver.finish(1, expired);
        QCOMPARE(player.snapshot().failure->error, PlaybackError::Expired);
        QVERIFY(backend.loads.isEmpty());
        QVERIFY(player.start({context, 3}, {{"3"}}));
        resolver.finish(2, source("WRONG"));
        QCOMPARE(player.snapshot().failure->error, PlaybackError::InvalidResponse);
    }
    void cannotReuseLoadIdentity()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        const LoadToken token{session.snapshot().token, 1};
        QVERIFY(player.start(token, {{"1"}}));
        QVERIFY(!player.start(token, {{"2"}}));
        player.stop();
        QVERIFY(!player.start(token, {{"3"}}));
        QVERIFY(player.start({token.session, 2}, {{"3"}}));
        QCOMPARE(resolver.requests.size(), 2);
    }
    void reentrantStopDuringPublication()
    {
        application::SessionCoordinator session;
        test::FakeStreamResolver resolver;
        test::FakePlaybackBackend backend;
        application::PlaybackCoordinator player(session, resolver, backend);
        connect(&player, &application::PlaybackCoordinator::changed, &player,
                [&](PlaybackSnapshot value) {
                    if (value.state == PlaybackState::Loading)
                        player.stop();
                });
        QVERIFY(player.start({session.snapshot().token, 1}, {{"1"}}));
        resolver.finish(0, source("1"));
        QVERIFY(backend.loads.isEmpty());
        QCOMPARE(player.snapshot().state, PlaybackState::Empty);
    }
};
QTEST_GUILESS_MAIN(PlaybackTest)
#include "playback_test.moc"
