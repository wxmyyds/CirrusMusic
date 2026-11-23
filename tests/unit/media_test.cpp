#include "infrastructure/media/media_source_policy.h"
#include <QtTest>
using namespace cirrus;
using domain::SourceProvider;

// The CDN address rules every audio download goes through (AudioStore; its redirect and
// header rules are in tst_audio_store), grouped by the issuing provider.
class MediaTest : public QObject
{
    Q_OBJECT
  private slots:
    void neteaseRefusals_data()
    {
        QTest::addColumn<QString>("url");
        for (const auto* url :
             {"file:///etc/passwd", "http://m802.music.126.net/a", "https://localhost/a",
              "https://m802.music.126.net.evil.example/a", "https://evil.example/music.126.net/a",
              "https://user:password@m802.music.126.net/a", "https://m802.music.126.net:8443/a",
              "https://m802.music.126.net/a#fragment", "https://127.0.0.1/a",
              "https://m802.music.126.net/", "https://m802.bilivideo.com/a"})
            QTest::newRow(url) << QString::fromLatin1(url);
    }
    void neteaseRefusals()
    {
        QFETCH(QString, url);
        QVERIFY(!media::approvedMediaUrl(SourceProvider::Netease, QUrl(url)));
    }
    void neteaseAccepts()
    {
        const auto url = media::approvedMediaUrl(SourceProvider::Netease,
                                                 QUrl("https://m802.music.126.net/a.mp3"));
        QVERIFY(url);
        // An initial http address from the provider is upgraded; a redirect is not.
        const auto upgraded = media::approvedMediaUrl(
            SourceProvider::Netease, QUrl("http://m802.music.126.net/a.mp3"), true);
        QVERIFY(upgraded);
        QCOMPARE(upgraded->scheme(), QStringLiteral("https"));
    }
    // Bilibili: its three CDNs, http and ports allowed, still no userinfo/fragment.
    void bilibiliAccepts()
    {
        for (const auto* url : {"https://upos-sz-mirrorcos.bilivideo.com/a.m4s",
                                "http://upos-hz-mirrorakam.akamaized.bilivideo.cn/a.m4s",
                                "https://xy.mountaintoys.cn:8443/a.m4s",
                                "https://cn-hbxy-cu-01.bilivideo.com/a.m4s"}) {
            QVERIFY2(media::approvedMediaUrl(SourceProvider::Bilibili,
                                             QUrl(QString::fromLatin1(url))),
                     url);
        }
    }
    void bilibiliRefusals_data()
    {
        QTest::addColumn<QString>("url");
        for (const auto* url : {"https://user:password@bilivideo.com/a.m4s",
                                "https://bilivideo.com/a.m4s#fragment", "https://m802.music.126.net/a",
                                "https://i0.hdslb.com/a", "https://example.akamaized.net/a",
                                "https://bilivideo.com/", "https://bilivideo.com.evil.example/a",
                                "ftp://bilivideo.com/a"})
            QTest::newRow(url) << QString::fromLatin1(url);
    }
    void bilibiliRefusals()
    {
        QFETCH(QString, url);
        QVERIFY(!media::approvedMediaUrl(SourceProvider::Bilibili, QUrl(url)));
    }
};
QTEST_GUILESS_MAIN(MediaTest)
#include "media_test.moc"
