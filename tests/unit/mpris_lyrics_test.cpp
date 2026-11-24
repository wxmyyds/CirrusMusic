#include "../support/presentation_playback_snapshot.h"
#include "desktop.h"
#include "lyrics.h"
#include "mpris.h"
#include "player.h"
#include "single_instance.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QSignalSpy>
#include <QTest>
#include <utility>

using namespace cirrus;

class LyricsSource final : public ui::LyricsSource
{
  public:
    QList<ui::Reply<ui::SongLyrics>> replies;
    ui::Request lyrics(const QString&, ui::Reply<ui::SongLyrics> reply) override
    {
        const auto active = std::make_shared<bool>(true);
        replies.append([active, reply](ui::Outcome<ui::SongLyrics> outcome) {
            if (*active)
                reply(std::move(outcome));
        });
        return ui::Request([active] { *active = false; });
    }
    void finish(int index, QString text)
    {
        ui::SongLyrics lyrics;
        lyrics.original.standard = ui::LyricTrack{};
        for (const auto& line : text.split('\n'))
            lyrics.original.standard->lines.append({0, 1000, line, {}});
        auto reply = std::exchange(replies[index], {});
        if (reply)
            reply(ui::Outcome<ui::SongLyrics>{lyrics});
    }
};

class MprisLyricsTest : public QObject
{
    Q_OBJECT
  signals:
    void propertiesSeen(QString interface, QVariantMap changed, QStringList invalidated);

  private slots:
    void currentLyricsAreExportedAndRetired()
    {
        test::UiPlaybackSnapshot source;
        LyricsSource lyricsSource;
        ui::Player player(&source, nullptr);
        ui::CurrentLyrics lyrics(&lyricsSource, &player);
        ui::SingleInstance instance;
        ui::Desktop desktop(&instance);
        ui::Mpris mpris(&player, &desktop, &lyrics);
        QVERIFY(mpris.registered()); // CI provides a private session bus.
        const auto metadata = [&] {
            return mpris.playerProperties().value("Metadata").toMap();
        };
        QVERIFY(!metadata().contains("xesam:asText"));
        QSignalSpy events(this, &MprisLyricsTest::propertiesSeen);
        QVERIFY(QDBusConnection::sessionBus().connect(
            "", "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties",
            "PropertiesChanged", this,
            SIGNAL(propertiesSeen(QString,QVariantMap,QStringList))));

        ui::PlaybackSnapshot state;
        state.current.key = "1";
        state.current.title = "Song";
        state.generation = 1;
        state.provider = ui::playback::Bilibili;
        state.status = ui::playback::Playing;
        source.publish(state);
        QVERIFY(!metadata().contains("xesam:asText"));
        QCOMPARE(lyricsSource.replies.size(), 1);
        lyricsSource.finish(0, "Bilibili subtitle\nSecond line");
        QCOMPARE(metadata().value("xesam:asText").toString(),
                 QString("Bilibili subtitle\nSecond line"));
        QTRY_VERIFY(!events.isEmpty());
        const auto announcesText = [&] {
            for (const auto& event : events) {
                const auto changed = event.at(1).toMap();
                if (qdbus_cast<QVariantMap>(changed.value("Metadata"))
                        .value("xesam:asText").toString() ==
                    "Bilibili subtitle\nSecond line")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY(announcesText());

        // A new load of the same song must not inherit its old video's subtitle.
        ++state.generation;
        source.publish(state);
        QVERIFY(!metadata().contains("xesam:asText"));
        lyricsSource.finish(0, "Late old subtitle");
        QVERIFY(!metadata().contains("xesam:asText"));
        QCOMPARE(lyricsSource.replies.size(), 2);
        lyricsSource.finish(1, "Replacement subtitle");
        QCOMPARE(metadata().value("xesam:asText").toString(), QString("Replacement subtitle"));

        state.provider = ui::playback::Netease;
        source.publish(state);
        QVERIFY(!metadata().contains("xesam:asText"));
        lyricsSource.finish(2, "Netease lyrics");
        QCOMPARE(metadata().value("xesam:asText").toString(), QString("Netease lyrics"));

        ++state.generation;
        source.publish(state);
        QCOMPARE(lyricsSource.replies.size(), 4);
        lyricsSource.finish(3, ""); // valid empty lyrics must not leave the previous text
        QVERIFY(!metadata().contains("xesam:asText"));

        source.publish({});
        QVERIFY(!metadata().contains("xesam:asText"));
        QDBusConnection::sessionBus().disconnect(
            "", "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties",
            "PropertiesChanged", this,
            SIGNAL(propertiesSeen(QString,QVariantMap,QStringList)));
    }
};

QTEST_MAIN(MprisLyricsTest)
#include "mpris_lyrics_test.moc"
