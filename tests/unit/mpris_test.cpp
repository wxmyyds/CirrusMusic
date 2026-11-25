#include "../support/presentation_playback_snapshot.h"
#include "desktop.h"
#include "mpris.h"
#include "player.h"
#include "single_instance.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUuid>

using namespace cirrus;

namespace
{
const QString kService = QStringLiteral("org.mpris.MediaPlayer2.cirrus");
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kProperties = QStringLiteral("org.freedesktop.DBus.Properties");

QVariantMap busMap(QVariant value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value.metaType() == QMetaType::fromType<QDBusArgument>() ? qdbus_cast<QVariantMap>(value)
                                                                    : value.toMap();
}

ui::PlaybackSnapshot pausedSong()
{
    ui::PlaybackSnapshot state;
    state.current.key = "1";
    state.current.title = "Song";
    state.current.artists = {{"", "Artist"}};
    state.current.albumTitle = "Album";
    state.generation = 1;
    state.provider = ui::playback::Bilibili;
    state.status = ui::playback::Paused;
    state.positionMs = 1000;
    state.positionAt = ui::playback::now();
    state.rate = 0;
    state.durationMs = 20000;
    state.seekable = state.canNext = state.canPrevious = true;
    return state;
}

class CommandSource final : public ui::PlaybackSource
{
  public:
    ui::PlaybackSnapshot state = pausedSong();
    int toggles = 0, nextCalls = 0, previousCalls = 0;
    QList<qint64> seeks;
    double volume = -1;
    ui::playback::Order order = ui::playback::Sequential;
    ui::playback::Repeat repeat = ui::playback::RepeatOff;
    const ui::PlaybackSnapshot& snapshot() const override { return state; }
    void setChangedHandler(std::function<void()>) override {}
    void playSongs(const QStringList&, int, const ui::PlayOrigin&) override {}
    void playNow(const QString&, bool, const ui::PlayOrigin&) override {}
    void playVoices(const QStringList&, int) override {}
    bool canPlayVoices() const override { return false; }
    void playPause() override { ++toggles; }
    void next() override { ++nextCalls; }
    void previous() override { ++previousCalls; }
    void seek(qint64 ms) override { seeks.append(ms); }
    void playAt(int) override {}
    void removeAt(int) override {}
    void move(int, int) override {}
    bool addSongs(const QStringList&, bool, const ui::PlayOrigin&) override { return false; }
    void clearQueue() override {}
    void setVolume(double value) override { volume = value; }
    void setOrder(ui::playback::Order value) override { order = value; }
    void setRepeat(ui::playback::Repeat value) override { repeat = value; }
    void startFm() override {}
};
} // namespace

class MprisTest : public QObject
{
    Q_OBJECT
    QDBusConnection m_client{QString{}};
    QString m_clientName;

    QDBusMessage call(const QString& interface, const QString& method,
                      const QList<QVariant>& arguments = {}, const QString& path = kPath)
    {
        auto message = QDBusMessage::createMethodCall(kService, path, interface, method);
        message.setArguments(arguments);
        QDBusPendingCallWatcher pending(m_client.asyncCall(message, 2000));
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&pending, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(2500);
        if (!pending.isFinished())
            loop.exec();
        if (!pending.isFinished())
            return QDBusMessage::createError(QDBusError::Timeout, "Test D-Bus call timed out");
        return pending.reply();
    }

  signals:
    void propertiesSeen(QString interface, QVariantMap changed, QStringList invalidated);

  private slots:
    void initTestCase()
    {
        m_clientName = "mpris-client-" + QUuid::createUuid().toString(QUuid::Id128);
        m_client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_clientName);
        QVERIFY(m_client.isConnected()); // CI supplies dbus-run-session.
        QVERIFY(m_client.connect(kService, kPath, kProperties, "PropertiesChanged", this,
                                 SIGNAL(propertiesSeen(QString, QVariantMap, QStringList))));
    }

    void cleanupTestCase() { QDBusConnection::disconnectFromBus(m_clientName); }

    void metadataAndTrackChanges()
    {
        test::UiPlaybackSnapshot source;
        ui::Player player(&source, nullptr);
        ui::SingleInstance instance;
        ui::Desktop desktop(&instance);
        ui::Mpris mpris(&player, &desktop);
        QVERIFY(mpris.registered());
        auto state = pausedSong();
        source.publish(state);
        auto reply = call(kProperties, "Get", {kPlayer, QString("Metadata")});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        const auto metadata = busMap(reply.arguments().first());
        QCOMPARE(metadata.value("xesam:title").toString(), QString("Song"));
        QCOMPARE(metadata.value("xesam:artist").toStringList(), QStringList{"Artist"});
        QCOMPARE(metadata.value("xesam:album").toString(), QString("Album"));
        QCOMPARE(metadata.value("mpris:length").toLongLong(), qint64(20000000));
        QCOMPARE(metadata.value("mpris:trackid").value<QDBusObjectPath>().path(), mpris.trackId());
        QVERIFY(!metadata.contains("xesam:asText"));
        QCOMPARE(mpris.positionUs(), qint64(1000000));
        QSignalSpy events(this, &MprisTest::propertiesSeen);
        state.current.key = "2";
        state.current.title = "Next song";
        ++state.generation;
        source.publish(state);
        const auto sawMetadata = [&] {
            for (const auto& event : events) {
                const auto changed = event.at(1).toMap();
                const auto next = busMap(changed.value("Metadata"));
                if (event.at(0).toString() == kPlayer &&
                    next.value("xesam:title").toString() == "Next song" &&
                    !next.contains("xesam:asText") && !changed.contains("Position"))
                    return true;
            }
            return false;
        };
        QTRY_VERIFY(sawMetadata());
        source.publish({});
        const auto empty = mpris.playerProperties().value("Metadata").toMap();
        QCOMPARE(empty.size(), 1);
        QCOMPARE(empty.value("mpris:trackid").value<QDBusObjectPath>().path(),
                 QString("/org/mpris/MediaPlayer2/TrackList/NoTrack"));
        // The removed detailed-lyrics object is no longer an alternate export endpoint.
        QCOMPARE(call(kProperties, "GetAll", {QString("org.cirrus.Lyrics1")}, "/org/cirrus/Lyrics")
                     .type(),
                 QDBusMessage::ErrorMessage);
    }

    void standardCommands()
    {
        CommandSource source;
        ui::Player player(&source, nullptr);
        ui::SingleInstance instance;
        ui::Desktop desktop(&instance);
        ui::Mpris mpris(&player, &desktop);
        QVERIFY(mpris.registered());
        const auto properties = mpris.playerProperties();
        for (const QString& key :
             {QString("CanControl"), QString("CanPlay"), QString("CanPause"), QString("CanSeek"),
              QString("CanGoNext"), QString("CanGoPrevious")})
            QVERIFY(properties.value(key).toBool());
        for (const QString& command : {QString("PlayPause"), QString("Next"), QString("Previous")})
            QCOMPARE(call(kPlayer, command).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(call(kPlayer, "Seek", {qlonglong(1500000)}).type(), QDBusMessage::ReplyMessage);
        const auto track = QVariant::fromValue(QDBusObjectPath(mpris.trackId()));
        QCOMPARE(call(kPlayer, "SetPosition", {track, qlonglong(2500000)}).type(),
                 QDBusMessage::ReplyMessage);
        QCOMPARE(call(kPlayer, "SetPosition",
                      {QVariant::fromValue(QDBusObjectPath("/stale")), qlonglong(3000000)})
                     .type(),
                 QDBusMessage::ReplyMessage);
        QCOMPARE(call(kPlayer, "SetPosition", {track, qlonglong(-1)}).type(),
                 QDBusMessage::ReplyMessage);
        QCOMPARE(call(kPlayer, "Seek", {qlonglong(30000000)}).type(), QDBusMessage::ReplyMessage);
        for (const auto& setting : QList<QPair<QString, QVariant>>{
                 {"Volume", 0.25}, {"Shuffle", true}, {"LoopStatus", QString("Track")}})
            QCOMPARE(
                call(kProperties, "Set",
                     {kPlayer, setting.first, QVariant::fromValue(QDBusVariant(setting.second))})
                    .type(),
                QDBusMessage::ReplyMessage);
        QCOMPARE(source.toggles, 1);
        QCOMPARE(source.nextCalls, 2); // Next and Seek beyond the end.
        QCOMPARE(source.previousCalls, 1);
        QCOMPARE(source.seeks, QList<qint64>({2500, 2500}));
        QCOMPARE(source.volume, 0.25);
        QCOMPARE(source.order, ui::playback::Shuffle);
        QCOMPARE(source.repeat, ui::playback::RepeatOne);
    }
};

QTEST_MAIN(MprisTest)
#include "mpris_test.moc"
