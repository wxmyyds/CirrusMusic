#include "infrastructure/bilibili/bili_wbi.h"
#include <QTest>
#include <QUrl>
#include <QUrlQuery>
using namespace cirrus::bilibili;

class BiliWbiTest : public QObject
{
    Q_OBJECT
  private slots:
    void mixinKeyFromKnownSample()
    {
        // The two file names from the reference WBI sample; the mixin key is the 32-character
        // prefix of their permutation.
        QCOMPARE(
            WbiSigner::mixinKey(
                "https://i0.hdslb.com/bfs/wbi/7cd084941338484aae1ad9425b84077c.png",
                "https://i0.hdslb.com/bfs/wbi/4932caff0ff746eab6f01bf08b70ac45.png"),
            QStringLiteral("ea1db124af3c7062474693fa704f4ff8"));
        // Bare names work the same.
        QCOMPARE(WbiSigner::mixinKey("7cd084941338484aae1ad9425b84077c",
                                     "4932caff0ff746eab6f01bf08b70ac45"),
                 QStringLiteral("ea1db124af3c7062474693fa704f4ff8"));
        // An unusable key is empty, not a partial string.
        QVERIFY(WbiSigner::mixinKey({}, {}).isEmpty());
    }

    void signMatchesKnownVector()
    {
        // The reference vector: bar/foo/zab, wts fixed, mixin from the sample above.
        QUrl url(QStringLiteral("https://api.bilibili.com/x/test"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("foo"), QStringLiteral("114"));
        query.addQueryItem(QStringLiteral("bar"), QStringLiteral("514"));
        query.addQueryItem(QStringLiteral("zab"), QStringLiteral("1919810"));
        url.setQuery(query);
        const auto signedUrl =
            WbiSigner::sign(url, QStringLiteral("ea1db124af3c7062474693fa704f4ff8"), 1702204169);
        QUrlQuery signedQuery(signedUrl);
        QCOMPARE(signedQuery.queryItemValue(QStringLiteral("w_rid")),
                 QStringLiteral("8f6f2b5b3d485fe1886cec6a0be8c5d4"));
        QCOMPARE(signedQuery.queryItemValue(QStringLiteral("wts")), QStringLiteral("1702204169"));
        // The original parameters survive.
        QCOMPARE(signedQuery.queryItemValue(QStringLiteral("bar")), QStringLiteral("514"));
        QCOMPARE(signedQuery.queryItemValue(QStringLiteral("zab")), QStringLiteral("1919810"));
    }

    void badKeyPassesThrough()
    {
        QUrl url(QStringLiteral("https://api.bilibili.com/x/test?a=1"));
        QCOMPARE(WbiSigner::sign(url, QStringLiteral("too-short"), 1702204169), url);
    }

    void separatorsStayInsideTheirValue()
    {
        QUrl url(QStringLiteral("https://api.bilibili.com/x/test?q=A%26B%3DC&wts=1&w_rid=old"));
        const auto signedUrl =
            WbiSigner::sign(url, QStringLiteral("ea1db124af3c7062474693fa704f4ff8"), 1702204169);
        const QUrlQuery query(signedUrl);
        QCOMPARE(query.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded), QStringLiteral("A&B=C"));
        QCOMPARE(query.allQueryItemValues(QStringLiteral("wts")).size(), 1);
        QCOMPARE(query.allQueryItemValues(QStringLiteral("w_rid")).size(), 1);
    }

    void filteredCharactersAreDropped()
    {
        // !'()* are removed from values before they are hashed and signed.
        QUrl url(QStringLiteral("https://api.bilibili.com/x/test?q=hello!'()*world"));
        const auto signedUrl =
            WbiSigner::sign(url, QStringLiteral("ea1db124af3c7062474693fa704f4ff8"), 1702204169);
        const QUrlQuery signedQuery(signedUrl);
        QCOMPARE(signedQuery.queryItemValue(QStringLiteral("q")), QStringLiteral("helloworld"));
        QVERIFY(!signedQuery.queryItemValue(QStringLiteral("w_rid")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(BiliWbiTest)
#include "bili_wbi_test.moc"
