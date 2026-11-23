#include "share_text.h"
#include <QtTest>

using namespace cirrus::ui::sharing;

class ShareTest : public QObject
{
    Q_OBJECT
  private slots:
    void links()
    {
        QCOMPARE(link(Kind::Song, "186016"), "https://music.163.com/song?id=186016");
        QCOMPARE(link(Kind::Playlist, "1"), "https://music.163.com/playlist?id=1");
        QCOMPARE(link(Kind::Album, "2"), "https://music.163.com/album?id=2");
        QCOMPARE(link(Kind::Artist, "6452"), "https://music.163.com/artist?id=6452");
        QCOMPARE(link(Kind::Podcast, "3"), "https://music.163.com/djradio?id=3");
        QCOMPARE(link(Kind::Voice, "4"), "https://music.163.com/program?id=4");
        QCOMPARE(link(Kind::User, "5"), "https://music.163.com/user/home?id=5");
        for (const char* bad : {"", "0", "12a", "1 2", "-1", "123456789012345678901"})
            QVERIFY(link(Kind::Song, bad).isEmpty());
    }
    void expand_data()
    {
        QTest::addColumn<QString>("templ");
        QTest::addColumn<QString>("author");
        QTest::addColumn<QString>("expected");
        const QString url = "https://music.163.com/song?id=1";
        QTest::newRow("default") << defaultTemplate() << "周杰伦"
                                 << "分享周杰伦的单曲《晴天》：" + url;
        QTest::newRow("default without author") << defaultTemplate() << ""
                                                << "分享单曲《晴天》：" + url;
        QTest::newRow("link only") << "{链接}" << "" << url;
        QTest::newRow("no link") << "{名称} - {作者}" << "周杰伦" << "晴天 - 周杰伦 " + url;
        QTest::newRow("empty") << "" << "" << url;
        QTest::newRow("link left out") << "[{作者}：{链接}]" << "" << url;
        QTest::newRow("text in brackets") << "[听听]{名称}" << "" << "听听晴天 " + url;
        QTest::newRow("nested") << "{名称}[ - {作者}[（{类型}）]] {链接}" << "周杰伦"
                                << "晴天 - 周杰伦（单曲） " + url;
        QTest::newRow("unclosed") << "[{名称} {链接}" << "" << "[晴天 " + url;
        QTest::newRow("stray close") << "{名称}] {链接}" << "" << "晴天] " + url;
        QTest::newRow("unknown placeholder") << "{歌词}{名称} {链接}" << "" << "{歌词}晴天 " + url;
        QTest::newRow("unclosed brace") << "{名称 {链接}" << "" << "{名称 " + url;
    }
    void expand()
    {
        QFETCH(QString, templ);
        QFETCH(QString, author);
        QFETCH(QString, expected);
        const Fields fields{link(Kind::Song, "1"), "晴天", author, typeName(Kind::Song)};
        QCOMPARE(cirrus::ui::sharing::expand(templ, fields), expected);
    }
};
QTEST_GUILESS_MAIN(ShareTest)
#include "share_test.moc"
