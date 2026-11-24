#include "infrastructure/bilibili/bili_lyrics.h"
#include "../support/synthetic_bilibili.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace cirrus;
using namespace cirrus::bilibili;
using namespace cirrus::domain;
using namespace cirrus::application;

namespace
{
PlaybackSourceSelection source()
{
    return {{{QUuid::createUuid(), 1}, 2}, {"1"}, SourceProvider::Bilibili,
            BiliStreamMeta{"BV1testpage", "456", "30280", {}}};
}
QJsonObject discovery(QString url, QString language = "zh-CN")
{
    return {{"subtitle", QJsonObject{{"subtitles", QJsonArray{
        QJsonObject{{"lan", language}, {"subtitle_url", url}}}}}}};
}
QByteArray envelope(const QJsonObject& data)
{
    return QJsonDocument(QJsonObject{{"code", 0}, {"data", data}}).toJson();
}
} // namespace

class BiliLyricsTest : public QObject
{
    Q_OBJECT
  private slots:
    void trustIsSeparateFromAudio()
    {
        QVERIFY(subtitleUrl(discovery("//aisubtitle.hdslb.com/bfs/subtitle/a.json")));
        for (const auto* url : {"https://evilhdslb.com/a", "https://cn.bilivideo.com/a",
                               "https://user@aisubtitle.hdslb.com/a", "http://i0.hdslb.com/a",
                               "https://i0.hdslb.com:443/a", "https://i0.hdslb.com/a#x"})
            QVERIFY(!subtitleUrl(discovery(QString::fromLatin1(url))));
    }
    void parseTimingWithoutInventingLyrics()
    {
        const auto track = subtitleTrack({{"body", QJsonArray{
            QJsonObject{{"from", 1.25}, {"to", 2.5}, {"content", "first"}},
            QJsonObject{{"from", 1.25}, {"to", 2.0}, {"content", "overlap"}},
            QJsonObject{{"from", -1}, {"to", 2}, {"content", "bad"}}}}});
        QCOMPARE(track.availability, LyricAvailability::Present);
        QCOMPARE(track.timing, LyricTiming::Lines);
        QCOMPARE(track.lines.size(), 2);
        QCOMPARE(track.lines.first().startMs, std::optional<qint64>(1250));
        QCOMPARE(track.lines.first().endMs, std::optional<qint64>(2500));
        QVERIFY(track.lines.first().words.isEmpty());
        QCOMPARE(track.malformedLines, 1);
        QCOMPARE(subtitleTrack({{"body", QJsonArray{}}}).availability, LyricAvailability::Empty);
        QCOMPARE(subtitleTrack({{"body", QJsonArray{QJsonObject{
            {"from", 0}, {"to", 2}, {"content", "   "}}}}}).availability, LyricAvailability::Empty);
        QCOMPARE(subtitleTrack({}).availability, LyricAvailability::Malformed);
    }
    void selectedSubtitleNeverReceivesCookie()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        fake->plans = {test::biliJson(envelope(discovery("//aisubtitle.hdslb.com/a.json"))),
                       test::biliJson(QJsonDocument(QJsonObject{{"body", QJsonArray{
                           QJsonObject{{"from", 0}, {"to", 3}, {"content", "synthetic"}}}}}).toJson())};
        Lyrics lyrics(std::move(manager));
        lyrics.setCookie("SESSDATA=synthetic");
        QSignalSpy done(&lyrics, &Lyrics::completed);
        const auto selected = source();
        const auto id = lyrics.request(selected);
        QVERIFY(id);
        QCOMPARE(fake->requests.size(), 0);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(fake->requests.size(), 2);
        const auto playerUrl = fake->requests.first().url();
        QCOMPARE(playerUrl.host(), QString("api.bilibili.com"));
        QCOMPARE(playerUrl.path(), QString("/x/player/wbi/v2"));
        const QUrlQuery query(playerUrl);
        QCOMPARE(query.queryItemValue("bvid"), selected.bili->bvid);
        QCOMPARE(query.queryItemValue("cid"), selected.bili->cid);
        QCOMPARE(fake->requests.last().url(), QUrl("https://aisubtitle.hdslb.com/a.json"));
        QCOMPARE(fake->requests.first().rawHeader("Cookie"), QByteArray("SESSDATA=synthetic"));
        QVERIFY(!fake->requests.last().hasRawHeader("Cookie"));
        const auto result = qvariant_cast<MusicResult>(done.first().at(1));
        QVERIFY(std::holds_alternative<SongLyrics>(result));
        QCOMPARE(std::get<SongLyrics>(result).original.standard.lines.size(), 1);
        QCOMPARE(std::get<SongLyrics>(result).translation.standard.availability,
                 LyricAvailability::Missing);
    }
    void concurrentSelectionsKeepTheirOwnSubtitles_data()
    {
        QTest::addColumn<bool>("sameVideo");
        QTest::newRow("different-videos") << false;
        QTest::newRow("different-pages-of-same-video") << true;
    }
    void concurrentSelectionsKeepTheirOwnSubtitles()
    {
        QFETCH(bool, sameVideo);
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        auto firstDiscovery = test::biliJson(envelope(discovery("//aisubtitle.hdslb.com/first.json")));
        firstDiscovery.delayMs = 250;
        const auto body = [](const char* content) {
            return test::biliJson(QJsonDocument(QJsonObject{{"body", QJsonArray{
                QJsonObject{{"from", 0}, {"to", 3}, {"content", QString::fromLatin1(content)}}}}}).toJson());
        };
        // Both player requests dispatch first. The second discovery and body finish before
        // the first discovery, exercising the per-request HTTP-to-selection mapping.
        fake->plans = {firstDiscovery,
                       test::biliJson(envelope(discovery("//aisubtitle.hdslb.com/second.json"))),
                       body("Second page subtitle"), body("First page subtitle")};
        Lyrics lyrics(std::move(manager));
        QSignalSpy done(&lyrics, &Lyrics::completed);
        const auto first = source();
        auto second = first;
        ++second.token.generation;
        second.song = {"2"};
        if (!sameVideo)
            second.bili->bvid = "BV2testpage";
        second.bili->cid = "789";
        const auto firstId = lyrics.request(first);
        const auto secondId = lyrics.request(second);
        QVERIFY(firstId && secondId && firstId != secondId);
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(fake->requests.size(), 4);
        for (int i = 0; i < 2; ++i) {
            const auto& selected = i == 0 ? first : second;
            const auto url = fake->requests.at(i).url();
            QCOMPARE(url.path(), QString("/x/player/wbi/v2"));
            QCOMPARE(QUrlQuery(url).queryItemValue("bvid"), selected.bili->bvid);
            QCOMPARE(QUrlQuery(url).queryItemValue("cid"), selected.bili->cid);
        }
        QCOMPARE(fake->requests.at(2).url(), QUrl("https://aisubtitle.hdslb.com/second.json"));
        QCOMPARE(fake->requests.at(3).url(), QUrl("https://aisubtitle.hdslb.com/first.json"));
        QCOMPARE(done.first().at(0).toULongLong(), secondId);
        QCOMPARE(done.last().at(0).toULongLong(), firstId);
        for (const auto& reply : done) {
            const bool isFirst = reply.at(0).toULongLong() == firstId;
            const auto result = qvariant_cast<MusicResult>(reply.at(1));
            QVERIFY(std::holds_alternative<SongLyrics>(result));
            const auto& track = std::get<SongLyrics>(result);
            QCOMPARE(track.song, isFirst ? first.song : second.song);
            QCOMPARE(track.original.standard.lines.first().text,
                     QString(isFirst ? "First page subtitle" : "Second page subtitle"));
        }
    }
    void cancelAfterSubtitleDispatch()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        auto body = test::biliJson("{\"body\":[{\"from\":0,\"to\":3,\"content\":\"stale\"}]}");
        body.delayMs = 250;
        fake->plans = {test::biliJson(envelope(discovery("//aisubtitle.hdslb.com/stale.json"))), body};
        Lyrics lyrics(std::move(manager));
        QSignalSpy done(&lyrics, &Lyrics::completed);
        const auto id = lyrics.request(source());
        QVERIFY(id);
        QTRY_COMPARE(fake->requests.size(), 2);
        lyrics.cancel(id);
        QTest::qWait(300);
        QCOMPARE(done.size(), 0);
    }
    void missingAndRiskControlAreFinal()
    {
        for (const bool risk : {false, true}) {
            auto manager = std::make_unique<test::FakeGetManager>();
            auto* fake = manager.get();
            auto plan = test::biliJson(envelope({}));
            if (risk)
                plan.status = 412;
            fake->plans = {plan};
            Lyrics lyrics(std::move(manager));
            QSignalSpy done(&lyrics, &Lyrics::completed);
            QVERIFY(lyrics.request(source()));
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(fake->requests.size(), 1);
            const auto result = qvariant_cast<MusicResult>(done.first().at(1));
            if (risk)
                QVERIFY(std::holds_alternative<MusicFailure>(result));
            else
                QCOMPARE(std::get<SongLyrics>(result).providerNoLyric, std::optional<bool>(true));
        }
    }
    void malformedSubtitleIsNotAbsence()
    {
        for (const bool unsafeUrl : {false, true}) {
            auto manager = std::make_unique<test::FakeGetManager>();
            auto* fake = manager.get();
            fake->plans = {test::biliJson(envelope(discovery(
                unsafeUrl ? "https://untrusted.example/a.json" : "//aisubtitle.hdslb.com/a.json"))),
                test::biliJson("{\"body\":[{\"from\":-1,\"to\":2,\"content\":\"bad\"}]}")};
            Lyrics lyrics(std::move(manager));
            QSignalSpy done(&lyrics, &Lyrics::completed);
            QVERIFY(lyrics.request(source()));
            QTRY_COMPARE(done.size(), 1);
            const auto result = qvariant_cast<MusicResult>(done.first().at(1));
            QVERIFY(std::holds_alternative<MusicFailure>(result));
            QCOMPARE(std::get<MusicFailure>(result).error, MusicError::InvalidResponse);
            QCOMPARE(fake->requests.size(), unsafeUrl ? 1 : 2);
        }
    }
    void fractionalDiscoveryCodeIsNotAbsence()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        manager->plans = {test::biliJson("{\"code\":0.5,\"data\":{}}")};
        Lyrics lyrics(std::move(manager));
        QSignalSpy done(&lyrics, &Lyrics::completed);
        QVERIFY(lyrics.request(source()));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(std::holds_alternative<MusicFailure>(qvariant_cast<MusicResult>(done.first().at(1))));
    }
    void cancelBeforeDispatch()
    {
        auto manager = std::make_unique<test::FakeGetManager>();
        auto* fake = manager.get();
        Lyrics lyrics(std::move(manager));
        QSignalSpy done(&lyrics, &Lyrics::completed);
        lyrics.cancel(lyrics.request(source()));
        QCoreApplication::processEvents();
        QCOMPARE(fake->requests.size(), 0);
        QCOMPARE(done.size(), 0);
    }
};
QTEST_GUILESS_MAIN(BiliLyricsTest)
#include "bili_lyrics_test.moc"
