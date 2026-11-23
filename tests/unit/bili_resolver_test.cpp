#include "infrastructure/bilibili/bili_stream_resolver.h"
#include "../support/synthetic_bilibili.h"

#include <QSignalSpy>
#include <QTest>

using namespace cirrus;
using namespace cirrus::application;
using namespace cirrus::domain;

namespace
{
class Catalog final : public IMusicGateway
{
  public:
    quint64 nextId = 0;
    bool refuse = false;
    QList<quint64> canceled;
    quint64 request(SessionToken, MusicQuery) override { return refuse ? 0 : ++nextId; }
    void cancel(quint64 id) override
    {
        canceled.append(id);
        emit completed(id, {}, MusicFailure{MusicError::Canceled});
    }
};

SongDetails details()
{
    Song song;
    song.id = {QStringLiteral("1")};
    song.title = QStringLiteral("Song");
    song.artists = {{std::nullopt, QStringLiteral("Artist")}};
    song.durationMs = 200000;
    return {{song}, {}};
}
} // namespace

class BiliResolverTest : public QObject
{
    Q_OBJECT
  private slots:
    void sourceMetadata_data()
    {
        QTest::addColumn<QByteArray>("mime");
        QTest::addColumn<QByteArray>("codec");
        QTest::addColumn<QString>("format");
        QTest::newRow("aac") << QByteArray("audio/mp4") << QByteArray("mp4a.40.2") << QString("aac");
        QTest::newRow("flac-mp4") << QByteArray("audio/mp4") << QByteArray("fLaC") << QString("flac-mp4");
        QTest::newRow("flac") << QByteArray("audio/flac") << QByteArray("fLaC") << QString("flac");
        QTest::newRow("dolby") << QByteArray("audio/mp4") << QByteArray("ec-3") << QString("eac3");
    }

    void sourceMetadata()
    {
        QFETCH(QByteArray, mime);
        QFETCH(QByteArray, codec);
        QFETCH(QString, format);
        Catalog catalog;
        auto manager = std::make_unique<test::FakeGetManager>();
        auto playurl = test::kBiliPlayUrlSized;
        playurl.replace("\"mimeType\":\"audio/mp4\"", "\"mimeType\":\"" + mime +
                                                    "\",\"codecs\":\"" + codec + "\"");
        manager->plans = test::biliResolveSized();
        manager->plans.last() = test::biliJson(playurl);
        bilibili::StreamResolver resolver(catalog, std::make_unique<bilibili::BiliApi>(std::move(manager)));
        QSignalSpy spy(&resolver, &IStreamResolver::completed);
        const SessionToken token{QUuid::createUuid(), 1};
        resolver.resolve(token, {{QStringLiteral("1")}, AudioQuality::Exhigh, true});
        emit catalog.completed(1, token, details());
        QTRY_COMPARE(spy.count(), 1);
        const auto result = qvariant_cast<StreamResult>(spy.first().at(2));
        QVERIFY(std::holds_alternative<StreamSource>(result));
        const auto& source = std::get<StreamSource>(result);
        QCOMPARE(source.provider, SourceProvider::Bilibili);
        QCOMPARE(source.format, format);
        QCOMPARE(source.size, qint64(4800000));
        QCOMPARE(std::get<StreamKey>(source.identity).key,
                 QStringLiteral("bili-auto-BV1testpage-456-id-30280"));
        QVERIFY(contentMd5(source).isEmpty());
        QVERIFY(source.bili);
        QCOMPARE(source.bili->bvid, QStringLiteral("BV1testpage"));
        QCOMPARE(source.bili->cid, QStringLiteral("456"));
        QCOMPARE(source.referer, QString::fromLatin1(bilibili::kBiliReferer));
        QVERIFY(!source.validUntil.hasExpired());
        QCOMPARE(qvariant_cast<SessionToken>(spy.first().at(1)), token);
    }

    void synchronousCatalogCancelCannotComplete()
    {
        Catalog catalog;
        bilibili::StreamResolver resolver(catalog, nullptr);
        QSignalSpy spy(&resolver, &IStreamResolver::completed);
        const auto id = resolver.resolve({}, {{QStringLiteral("1")}});
        resolver.cancel(id);
        QCOMPARE(catalog.canceled, QList<quint64>{1});
        QCOMPARE(spy.count(), 0);
    }

    void refusedCatalogCompletionIsCancelable()
    {
        Catalog catalog;
        catalog.refuse = true;
        bilibili::StreamResolver resolver(catalog, nullptr);
        QSignalSpy spy(&resolver, &IStreamResolver::completed);
        resolver.cancel(resolver.resolve({}, {{QStringLiteral("1")}}));
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(BiliResolverTest)
#include "bili_resolver_test.moc"
