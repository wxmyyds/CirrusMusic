#include "infrastructure/bilibili/bili_matcher.h"
#include "infrastructure/bilibili/bili_models.h"
#include <QTest>
using namespace cirrus::bilibili;

class BiliMatcherTest : public QObject
{
    Q_OBJECT
  private slots:
    void queriesInOrderDeduped()
    {
        SongTargetInfo song{QStringLiteral("Song Title"), QStringLiteral("Artist"), 200000};
        const auto queries = buildQueries(song);
        QCOMPARE(queries, (QStringList{QStringLiteral("Song Title Artist"),
                                       QStringLiteral("Artist Song Title"),
                                       QStringLiteral("Song Title"),
                                       QStringLiteral("Song Title Artist 无损")}));
        // Same title as artist collapses the duplicate.
        QCOMPARE(buildQueries({QStringLiteral("X"), QStringLiteral("X"), 0}).size(), 3);
        // No artist: only the title.
        QCOMPARE(buildQueries({QStringLiteral("Only"), {}, 0}),
                 (QStringList{QStringLiteral("Only"), QStringLiteral("Only 无损")}));
    }

    void durationBuckets()
    {
        QCOMPARE(durationFilter(0), 0);
        QCOMPARE(durationFilter(5 * 60 * 1000), 1);
        QCOMPARE(durationFilter(15 * 60 * 1000), 2);
        QCOMPARE(durationFilter(45 * 60 * 1000), 3);
        QCOMPARE(durationFilter(90 * 60 * 1000), 4);
    }

    void scoreReachesThresholdOnRealMatch()
    {
        SongTargetInfo song{QStringLiteral("晴天"), QStringLiteral("周杰伦"), 206000};
        // Title hit (55) + artist hit (25) + close duration (30) = 110, well over 70.
        QVERIFY(score(song, QStringLiteral("晴天"), QStringLiteral("周杰伦"), 206) >= kMinimumScore);
        // A cover by another uploader: title only, no artist, wrong length → below threshold.
        QVERIFY(score(song, QStringLiteral("晴天 (翻唱)"), QStringLiteral("someone"), 300) <
                kMinimumScore);
    }

    void overlongCandidatePenalised()
    {
        SongTargetInfo song{QStringLiteral("Track"), QStringLiteral("Band"), 60000};
        // 5x the length: duration penalty, not a match.
        QVERIFY(score(song, QStringLiteral("Track"), QStringLiteral("Band"), 300) <
                kMinimumScore);
    }

    void overlongPagesAreExcluded()
    {
        SongTargetInfo song{QStringLiteral("Song"), QStringLiteral("Artist"), 60000};
        SearchItem item;
        item.title = song.title;
        item.author = song.artist;
        item.durationSec = 60;
        VideoInfo video;
        video.title = song.title;
        video.ownerName = song.artist;
        video.pages = {{1, 1, song.title, 300}};
        QVERIFY(!bestPage(song, item, video));
        video.pages = {{1, 1, song.title, 60}};
        item.durationSec = 300;
        QVERIFY(!bestPage(song, item, video));
    }

    void bestPagePicksHighestScoringPage()
    {
        SongTargetInfo song{QStringLiteral("Song"), QStringLiteral("Artist"), 200000};
        SearchItem item;
        item.title = QStringLiteral("Song");
        item.author = QStringLiteral("Artist");
        item.durationSec = 200;
        VideoInfo video;
        video.title = QStringLiteral("Song Full Album");
        video.ownerName = QStringLiteral("Artist");
        video.pages = {{111, 1, QStringLiteral("Song"), 200},
                       {222, 2, QStringLiteral("Unrelated"), 30}};
        const auto match = bestPage(song, item, video);
        QVERIFY(match.has_value());
        QCOMPARE(match->page.cid, qint64(111));
        QVERIFY(match->score >= kMinimumScore);
    }
};

QTEST_GUILESS_MAIN(BiliMatcherTest)
#include "bili_matcher_test.moc"
