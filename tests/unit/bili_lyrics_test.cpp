#include "infrastructure/bilibili/bili_lyrics.h"
#include "../support/synthetic_bilibili.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

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
        const auto id = lyrics.request(source());
        QVERIFY(id);
        QCOMPARE(fake->requests.size(), 0);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(fake->requests.size(), 2);
        QCOMPARE(fake->requests.first().rawHeader("Cookie"), QByteArray("SESSDATA=synthetic"));
        QVERIFY(!fake->requests.last().hasRawHeader("Cookie"));
        const auto result = qvariant_cast<MusicResult>(done.first().at(1));
        QVERIFY(std::holds_alternative<SongLyrics>(result));
        QCOMPARE(std::get<SongLyrics>(result).original.standard.lines.size(), 1);
        QCOMPARE(std::get<SongLyrics>(result).translation.standard.availability,
                 LyricAvailability::Missing);
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
