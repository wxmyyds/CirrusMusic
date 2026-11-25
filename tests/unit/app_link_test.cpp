#include "app_link.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace cirrus::ui::applink;

namespace
{
QString link(const QJsonObject& object, const QString& tail = {})
{
    return QStringLiteral("orpheus://") +
           QString::fromLatin1(QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64()) +
           tail;
}
} // namespace

class AppLinkTest : public QObject
{
    Q_OBJECT
  private slots:
    void targets_data()
    {
        QTest::addColumn<QString>("type");
        QTest::addColumn<int>("target");
        QTest::newRow("song") << "song" << int(Target::Song);
        QTest::newRow("program") << "program" << int(Target::Voice);
        QTest::newRow("dj") << "dj" << int(Target::Voice);
        QTest::newRow("playlist") << "playlist" << int(Target::Playlist);
        QTest::newRow("album") << "album" << int(Target::Album);
        QTest::newRow("artist") << "artist" << int(Target::Artist);
        QTest::newRow("djradio") << "djradio" << int(Target::Podcast);
        QTest::newRow("user/home") << "user/home" << int(Target::User);
    }
    void targets()
    {
        QFETCH(QString, type);
        QFETCH(int, target);
        const auto r = parse(link({{"type", type}, {"id", "186016"}, {"cmd", "play"}}));
        QVERIFY(r);
        QCOMPARE(int(r->target), target);
        QCOMPARE(r->id, "186016");
        QVERIFY(r->play);
    }
    void asTheWebSendsIt()
    {
        // The old web page's 去客户端播放: a numeric id is fine too, and the channel is ignored.
        auto r =
            parse(link({{"type", "song"}, {"id", 186016}, {"cmd", "play"}, {"channel", "webset"}}));
        QVERIFY(r && r->target == Target::Song && r->id == "186016" && r->play);
        r = parse(link({{"type", "album"}, {"id", "2"}, {"cmd", "download"}}, "/"));
        QVERIFY(r && r->target == Target::Album && !r->play);
        r = parse(link({{"type", "playlist"}, {"id", "3"}}, "?from=web"));
        QVERIFY(r && r->target == Target::Playlist);
        r = parse(link({{"type", "mv"}, {"id", "4"}, {"cmd", "play"}}));
        QVERIFY(r && r->target == Target::Unsupported && r->id.isEmpty());
        QVERIFY(
            parse(QStringLiteral("ORPHEUS://") + link({{"type", "song"}, {"id", "5"}}).mid(10)));
    }
    void refused_data()
    {
        QTest::addColumn<QString>("url");
        QTest::newRow("empty") << "";
        QTest::newRow("other scheme") << "https://music.163.com/song?id=1";
        QTest::newRow("not base64") << "orpheus://song/1";
        QTest::newRow("not json") << "orpheus://aGVsbG8=";
        QTest::newRow("bad id") << link({{"type", "song"}, {"id", "1;rm"}});
        QTest::newRow("no id") << link({{"type", "song"}});
        QTest::newRow("zero id") << link({{"type", "song"}, {"id", "0"}});
        QTest::newRow("fraction") << link({{"type", "song"}, {"id", 1.5}});
        QTest::newRow("space") << "orpheus://eyJ0 eXBlIjoic29uZyJ9";
        QTest::newRow("too long") << "orpheus://" + QString(3000, u'A');
    }
    void refused()
    {
        QFETCH(QString, url);
        QVERIFY(!parse(url));
    }
};
QTEST_GUILESS_MAIN(AppLinkTest)
#include "app_link_test.moc"
