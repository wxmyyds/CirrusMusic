#include "../support/bili_playurl_fixtures.h"
#include "infrastructure/bilibili/bili_playurl.h"
#include "infrastructure/bilibili/bili_quality.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
using namespace cirrus::bilibili;

namespace
{
QJsonObject parse(const QByteArray& json) { return QJsonDocument::fromJson(json).object(); }
} // namespace

class BiliQualityTest : public QObject
{
    Q_OBJECT
  private slots:
    void qualityKeys()
    {
        QCOMPARE(qualityFromKey(QStringLiteral("hires")), Quality::HiRes);
        QCOMPARE(qualityFromKey(QStringLiteral("lossless")), Quality::Lossless);
        QCOMPARE(qualityFromKey(QStringLiteral("medium")), Quality::Medium);
        QCOMPARE(qualityFromKey(QStringLiteral("low")), Quality::Low);
        QCOMPARE(qualityFromKey(QStringLiteral("exhigh")), Quality::High);
        QCOMPARE(qualityFromKey(QString()), Quality::High);
    }

    void hostPrioritisation()
    {
        const auto ordered = prioritize(QStringLiteral("https://cn.bilivideo.com/a"),
                                        {QStringLiteral("https://upos-sz.bilivideo.com/a"),
                                         QStringLiteral("https://x.mountaintoys.cn/a")});
        QCOMPARE(ordered.first(), QStringLiteral("https://upos-sz.bilivideo.com/a"));
        QCOMPARE(ordered.last(), QStringLiteral("https://x.mountaintoys.cn/a"));
        // Deduplicated.
        QCOMPARE(prioritize(QStringLiteral("https://cn.bilivideo.com/a"),
                            {QStringLiteral("https://cn.bilivideo.com/a")})
                     .size(),
                 1);
    }

    void selectionPrefersTag()
    {
        QList<AudioStream> streams;
        AudioStream regular;
        regular.id = 30280;
        regular.mimeType = QStringLiteral("audio/mp4");
        regular.bitrateKbps = 192;
        regular.url = QStringLiteral("https://a.bilivideo.com/r");
        streams.append(regular);
        AudioStream flac;
        flac.id = 30251;
        flac.mimeType = QStringLiteral("audio/flac");
        flac.qualityTag = QStringLiteral("hires");
        flac.bitrateKbps = 1200;
        flac.url = QStringLiteral("https://a.bilivideo.com/f");
        streams.append(flac);

        QCOMPARE(selectStream(streams, Quality::HiRes)->qualityTag, QStringLiteral("hires"));
        // Asking high degrades to the regular AAC, not up to the FLAC tag.
        QCOMPARE(selectStream(streams, Quality::High)->id, 30280);
        // Empty input is no stream.
        QVERIFY(!selectStream({}, Quality::High).has_value());
    }

    void applicationQualitiesDoNotPreferDolby()
    {
        auto streams = mapPlayUrl(parse(cirrus::test::kDashDolbyFlac)).result.streams;
        // Dolby entries are excluded before ordinary quality selection.
        for (const auto quality :
             {Quality::Medium, Quality::High, Quality::Lossless, Quality::HiRes}) {
            const auto selected = selectStream(streams, quality);
            QVERIFY(selected);
            QVERIFY(selected->qualityTag != QStringLiteral("dolby"));
        }
        // The ordinary AAC remains selectable even when no FLAC/Hi-Res track exists.
        streams.removeIf(
            [](const AudioStream& stream) { return stream.qualityTag == QStringLiteral("hires"); });
        for (const auto quality :
             {Quality::Medium, Quality::High, Quality::Lossless, Quality::HiRes}) {
            const auto selected = selectStream(streams, quality);
            QVERIFY(selected);
            QCOMPARE(selected->id, 30280);
        }
    }

    void selectionDegradesDownTheLadder()
    {
        QList<AudioStream> onlyLow;
        AudioStream low;
        low.mimeType = QStringLiteral("audio/mp4");
        low.bitrateKbps = 64;
        low.url = QStringLiteral("https://a.bilivideo.com/l");
        onlyLow.append(low);
        // Preferred hires but only a low track exists: it is the only answer, never null.
        QCOMPARE(selectStream(onlyLow, Quality::HiRes)->bitrateKbps, 64);
    }
};

class BiliPlayUrlTest : public QObject
{
    Q_OBJECT
  private slots:
    void mapsOrdinaryAudioAndIgnoresDolby()
    {
        const auto data = parse(cirrus::test::kDashDolbyFlac);
        const auto mapping = mapPlayUrl(data);
        QCOMPARE(mapping.result.streams.size(), 2);
        QVERIFY(mapping.result.videoAvailable);
        bool sawRegular = false, sawFlac = false;
        for (const auto& s : mapping.result.streams) {
            if (s.id == 30280) {
                sawRegular = true;
                QCOMPARE(s.size, qint64(4800000));
                QCOMPARE(s.md5, QByteArray("0123456789abcdef0123456789abcdef"));
            }
            QVERIFY(s.id != 30250);
            QVERIFY(s.qualityTag != QStringLiteral("dolby"));
            if (s.id == 30251) {
                sawFlac = true;
                QCOMPARE(s.qualityTag, QStringLiteral("hires"));
            }
        }
        QVERIFY(sawRegular && sawFlac);
    }

    void dolbyOnlyIsNotPlayable()
    {
        auto data = parse(cirrus::test::kDashDolbyFlac);
        auto dash = data.value(QStringLiteral("dash")).toObject();
        dash.remove(QStringLiteral("audio"));
        dash.remove(QStringLiteral("flac"));
        data.insert(QStringLiteral("dash"), dash);
        const auto mapping = mapPlayUrl(data);
        QVERIFY(mapping.result.videoAvailable);
        QVERIFY(mapping.result.streams.isEmpty());
    }

    void nearestUrlFirst()
    {
        const auto data = parse(cirrus::test::kPrioritizedDash);
        const auto mapping = mapPlayUrl(data);
        QCOMPARE(mapping.result.streams.size(), 1);
        QCOMPARE(mapping.result.streams.first().url,
                 QStringLiteral("https://upos-sz.bilivideo.com/a"));
    }

    void progressiveFallbackSingleEntry()
    {
        const auto data = parse(cirrus::test::kProgressive);
        const auto mapping = mapPlayUrl(data);
        QCOMPARE(mapping.result.streams.size(), 1);
        QCOMPARE(mapping.result.streams.first().mimeType, QStringLiteral("video/mp4"));
        QCOMPARE(mapping.result.streams.first().size, qint64(1250000));
        QVERIFY(mapping.result.videoAvailable);
    }

    void emptyAudioWithVideoStaysEmpty()
    {
        const auto data = parse(cirrus::test::kEmptyAudio);
        const auto mapping = mapPlayUrl(data);
        QVERIFY(mapping.result.streams.isEmpty());
        QVERIFY(mapping.result.videoAvailable);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int status = 0;
    {
        BiliQualityTest quality;
        status |= QTest::qExec(&quality, argc, argv);
    }
    {
        BiliPlayUrlTest playurl;
        status |= QTest::qExec(&playurl, argc, argv);
    }
    return status;
}

#include "bili_quality_test.moc"
