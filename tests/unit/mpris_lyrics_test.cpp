#include "../support/presentation_playback_snapshot.h"
#include "app_settings.h"
#include "desktop.h"
#include "lyrics.h"
#include "lyrics_dbus.h"
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
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <QXmlStreamReader>
#include <memory>
#include <utility>

using namespace cirrus;

namespace
{
const QString kService = QStringLiteral("org.mpris.MediaPlayer2.cirrus");
const QString kLyricsPath = QStringLiteral("/org/cirrus/Lyrics");
const QString kLyricsInterface = QStringLiteral("org.cirrus.Lyrics1");
const QString kPlayerPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

QVariant unbox(QVariant value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    return value;
}

QVariantMap busMap(QVariant value)
{
    value = unbox(std::move(value));
    return value.metaType() == QMetaType::fromType<QDBusArgument>()
               ? qdbus_cast<QVariantMap>(value) : value.toMap();
}

QVariantList busList(QVariant value)
{
    value = unbox(std::move(value));
    return value.metaType() == QMetaType::fromType<QDBusArgument>()
               ? qdbus_cast<QVariantList>(value) : value.toList();
}

ui::SongLyrics lineLyrics(const QString& text)
{
    ui::SongLyrics result;
    result.original.standard = ui::LyricTrack{};
    qint64 start = 1000;
    for (const QString& line : text.split('\n')) {
        result.original.standard->lines.append({start, start + 1000, line, {}});
        start += 1000;
    }
    return result;
}

ui::PlaybackSnapshot pausedSong()
{
    ui::PlaybackSnapshot state;
    state.current.key = "1";
    state.current.title = "Song";
    state.generation = 1;
    state.provider = ui::playback::Bilibili;
    state.status = ui::playback::Paused;
    state.positionAt = ui::playback::now();
    state.rate = 0; // Exact positions, without a QML PlaybackClock or wall-clock drift.
    state.durationMs = 20000;
    state.seekable = state.canNext = state.canPrevious = true;
    return state;
}

bool sawDocument(const QSignalSpy& events, quint64 revision, const QString& status,
                 const QString& provider)
{
    for (const auto& event : events) {
        if (event.at(0).toString() != kLyricsInterface)
            continue;
        const QVariantMap changed = event.at(1).toMap();
        if (!changed.contains("Document"))
            continue;
        const auto document = busMap(changed.value("Document"));
        if (document.value("revision").toULongLong() == revision &&
            document.value("status").toString() == status &&
            document.value("playbackProvider").toString() == provider)
            return true;
    }
    return false;
}
} // namespace

class LyricsSource final : public ui::LyricsSource
{
  public:
    QList<ui::Reply<ui::SongLyrics>> replies;
    int delivered = 0;
    ui::Request lyrics(const QString&, ui::Reply<ui::SongLyrics> reply) override
    {
        const auto active = std::make_shared<bool>(true);
        replies.append([this, active, reply](ui::Outcome<ui::SongLyrics> outcome) {
            if (*active) {
                ++delivered;
                reply(std::move(outcome));
            }
        });
        return ui::Request([active] { *active = false; });
    }
    void finishOutcome(int index, ui::Outcome<ui::SongLyrics> outcome)
    {
        auto reply = std::exchange(replies[index], {});
        if (reply)
            reply(std::move(outcome));
    }
    void finish(int index, ui::SongLyrics lyrics)
    {
        finishOutcome(index, ui::Outcome<ui::SongLyrics>{std::move(lyrics)});
    }
    void finish(int index, const QString& text) { finish(index, lineLyrics(text)); }
};

struct LyricsFixture {
    test::UiPlaybackSnapshot source;
    LyricsSource lyricsSource;
    ui::AppSettings settings;
    ui::Player player{&source, nullptr};
    ui::CurrentLyrics lyrics{&lyricsSource, &player};
    ui::SingleInstance instance;
    ui::Desktop desktop{&instance};
    ui::Mpris mpris;

    explicit LyricsFixture(bool withSettings = true)
        : mpris(&player, &desktop, &lyrics, withSettings ? &settings : nullptr) {}
    ui::LyricsDBus* exporter() { return mpris.findChild<ui::LyricsDBus*>(); }
    QVariantMap metadata() const { return mpris.playerProperties().value("Metadata").toMap(); }
};

// The shared snapshot fake is final; this command-recording fake stays local to this test.
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

class MprisLyricsTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_config;
    QDBusConnection m_client{QString{}};
    QString m_clientName;

    QDBusMessage call(const QString& path, const QString& interface, const QString& method,
                      const QList<QVariant>& arguments = {})
    {
        auto message = QDBusMessage::createMethodCall(kService, path, interface, method);
        message.setArguments(arguments);
        // A separate connection forces real bus marshaling. Never block the server's thread
        // with a synchronous call to this process's own service.
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
    void lyricsPropertiesSeen(QString interface, QVariantMap changed, QStringList invalidated);

  private slots:
    void initTestCase()
    {
        QVERIFY(m_config.isValid());
        // Both scopes are redirected before constructing any AppSettings/CurrentLyrics.
        // Unique names also prevent reading a real user's fallback configuration.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_config.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_config.path());
        const QString unique = QUuid::createUuid().toString(QUuid::Id128);
        QCoreApplication::setOrganizationName("CirrusMprisTest-" + unique);
        QCoreApplication::setOrganizationDomain("mpris-test.invalid");
        QCoreApplication::setApplicationName("Lyrics-" + unique);
        m_clientName = "mpris-lyrics-client-" + unique;
        m_client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_clientName);
        QVERIFY(m_client.isConnected()); // CI provides dbus-run-session.
        QVERIFY(m_client.connect(kService, kPlayerPath, kPropertiesInterface,
                                 "PropertiesChanged", this,
                                 SIGNAL(propertiesSeen(QString,QVariantMap,QStringList))));
        QVERIFY(m_client.connect(kService, kLyricsPath, kPropertiesInterface,
                                 "PropertiesChanged", this,
                                 SIGNAL(lyricsPropertiesSeen(QString,QVariantMap,QStringList))));
    }

    void init()
    {
        QSettings store;
        store.setFallbacksEnabled(false);
        store.clear(); // Only the unique, temporary test store, including per-song offsets.
        store.sync();
        QCOMPARE(store.status(), QSettings::NoError);
    }

    void cleanupTestCase()
    {
        m_client.disconnect(kService, kPlayerPath, kPropertiesInterface, "PropertiesChanged",
                            this, SIGNAL(propertiesSeen(QString,QVariantMap,QStringList)));
        m_client.disconnect(kService, kLyricsPath, kPropertiesInterface, "PropertiesChanged",
                            this, SIGNAL(lyricsPropertiesSeen(QString,QVariantMap,QStringList)));
        QDBusConnection::disconnectFromBus(m_clientName);
    }

    void currentLyricsAreExportedAndRetired()
    {
        LyricsFixture f;
        QVERIFY(f.mpris.registered());
        auto* exporter = f.exporter();
        QVERIFY(exporter);
        QVERIFY(exporter->findChild<ui::LyricsDBusAdaptor*>());
        QVERIFY(!f.settings.externalLyrics());
        QSignalSpy events(this, &MprisLyricsTest::propertiesSeen);
        QSignalSpy lyricEvents(this, &MprisLyricsTest::lyricsPropertiesSeen);
        auto state = pausedSong();
        f.source.publish(state);
        QCOMPARE(f.lyricsSource.replies.size(), 1);
        f.lyricsSource.finish(0, "Bilibili subtitle\nSecond line");
        QVERIFY(f.lyrics.matchesPlayback());
        QVERIFY(!f.metadata().contains("xesam:asText"));
        QVERIFY(exporter->document().isEmpty());
        QVERIFY(exporter->state().isEmpty());
        auto reply = call(kLyricsPath, kLyricsInterface, "GetSnapshot");
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().size(), 1);
        auto snapshot = busMap(reply.arguments().first());
        QCOMPARE(snapshot.value("version").metaType(), QMetaType::fromType<uint>());
        QCOMPARE(snapshot.value("version").toUInt(), uint(1));
        QCOMPARE(snapshot.value("enabled").metaType(), QMetaType::fromType<bool>());
        QVERIFY(!snapshot.value("enabled").toBool());
        QVERIFY(busMap(snapshot.value("document")).isEmpty());
        QVERIFY(busMap(snapshot.value("state")).isEmpty());

        auto standard = f.mpris.playerProperties();
        standard.remove("Metadata");
        f.settings.setExternalLyrics(true);
        QCOMPARE(f.metadata().value("xesam:asText").toString(),
                 QString("Bilibili subtitle\nSecond line"));
        auto enabledStandard = f.mpris.playerProperties();
        enabledStandard.remove("Metadata");
        QCOMPARE(enabledStandard, standard);
        const auto readyRevision = exporter->document().value("revision").toULongLong();
        QVERIFY(readyRevision > 0);
        QTRY_VERIFY(sawDocument(lyricEvents, readyRevision, "ready", "bilibili"));
        const auto announcesText = [&] {
            for (const auto& event : events)
                if (event.at(0).toString() == kPlayerInterface &&
                    busMap(event.at(1).toMap().value("Metadata")).value("xesam:asText") ==
                        "Bilibili subtitle\nSecond line")
                    return true;
            return false;
        };
        QTRY_VERIFY(announcesText());

        // Retire a ready document, then cancel a still-pending replacement of the same song.
        ++state.generation;
        f.source.publish(state);
        QVERIFY(!f.metadata().contains("xesam:asText"));
        QCOMPARE(exporter->document().value("status").toString(), QString("loading"));
        QVERIFY(busList(exporter->document().value("lines")).isEmpty());
        auto loadingRevision = exporter->document().value("revision").toULongLong();
        QVERIFY(loadingRevision > readyRevision);
        QTRY_VERIFY(sawDocument(lyricEvents, loadingRevision, "loading", "bilibili"));
        ++state.generation;
        f.source.publish(state);
        QCOMPARE(f.lyricsSource.replies.size(), 3);
        loadingRevision = exporter->document().value("revision").toULongLong();
        reply = call(kLyricsPath, kLyricsInterface, "GetSnapshot");
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        lyricEvents.clear();
        f.lyricsSource.finish(1, "Late canceled subtitle");
        QCOMPARE(f.lyricsSource.delivered, 1);
        QCOMPARE(exporter->document().value("revision").toULongLong(), loadingRevision);
        QVERIFY(!f.metadata().contains("xesam:asText"));
        reply = call(kLyricsPath, kLyricsInterface, "GetSnapshot");
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(busMap(busMap(reply.arguments().value(0)).value("document"))
                     .value("revision").toULongLong(), loadingRevision);
        QVERIFY(lyricEvents.isEmpty()); // A canceled completion must not announce another revision.
        f.lyricsSource.finish(2, "Replacement subtitle");
        QCOMPARE(f.metadata().value("xesam:asText").toString(), QString("Replacement subtitle"));
        QCOMPARE(exporter->document().value("generation").toULongLong(), state.generation);

        state.provider = ui::playback::Netease;
        f.source.publish(state);
        QCOMPARE(f.lyricsSource.replies.size(), 4);
        QVERIFY(!f.metadata().contains("xesam:asText"));
        loadingRevision = exporter->document().value("revision").toULongLong();
        QTRY_VERIFY(sawDocument(lyricEvents, loadingRevision, "loading", "netease"));
        f.lyricsSource.finish(3, "Netease lyrics");
        QCOMPARE(f.metadata().value("xesam:asText").toString(), QString("Netease lyrics"));

        events.clear();
        lyricEvents.clear();
        f.settings.setExternalLyrics(false);
        QVERIFY(exporter->document().isEmpty());
        QVERIFY(exporter->state().isEmpty());
        QVERIFY(!f.metadata().contains("xesam:asText"));
        QCOMPARE(f.metadata().value("xesam:title").toString(), QString("Song"));
        const auto clearingSeen = [&] {
            for (const auto& event : lyricEvents) {
                const auto changed = event.at(1).toMap();
                if (event.at(0).toString() == kLyricsInterface &&
                    changed.contains("Enabled") && !changed.value("Enabled").toBool() &&
                    changed.contains("Document") && busMap(changed.value("Document")).isEmpty() &&
                    changed.contains("State") && busMap(changed.value("State")).isEmpty() &&
                    event.at(2).toStringList().isEmpty())
                    return true;
            }
            return false;
        };
        const auto metadataCleared = [&] {
            for (const auto& event : events) {
                const auto changed = event.at(1).toMap();
                const auto metadata = busMap(changed.value("Metadata"));
                if (event.at(0).toString() == kPlayerInterface && changed.contains("Metadata") &&
                    metadata.value("xesam:title") == "Song" && !metadata.contains("xesam:asText"))
                    return true;
            }
            return false;
        };
        QTRY_VERIFY(clearingSeen());
        QTRY_VERIFY(metadataCleared());
        reply = call(kLyricsPath, kLyricsInterface, "GetSnapshot");
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        snapshot = busMap(reply.arguments().value(0));
        QVERIFY(!snapshot.value("enabled").toBool());
        QVERIFY(busMap(snapshot.value("document")).isEmpty());
        QVERIFY(busMap(snapshot.value("state")).isEmpty());
        f.settings.setExternalLyrics(true);
        QCOMPARE(f.metadata().value("xesam:asText").toString(), QString("Netease lyrics"));
        QVERIFY(exporter->document().value("revision").toULongLong() > loadingRevision);

        ++state.generation;
        f.source.publish(state);
        QCOMPARE(f.lyricsSource.replies.size(), 5);
        f.lyricsSource.finish(4, QString{});
        QVERIFY(!f.metadata().contains("xesam:asText"));
        QCOMPARE(exporter->document().value("status").toString(), QString("empty"));
        f.source.publish({});
        QCOMPARE(exporter->document().value("status").toString(), QString("idle"));
        QCOMPARE(exporter->state().value("playbackStatus").toString(), QString("stopped"));
        QCOMPARE(exporter->state().value("currentLine").toInt(), -1);
        QVERIFY(!f.metadata().contains("xesam:asText"));
    }

    void missingSettingsNeverOptIn()
    {
        LyricsFixture f(false);
        QVERIFY(f.mpris.registered());
        QVERIFY(f.exporter());
        f.settings.setExternalLyrics(true); // An unrelated settings object is not consent.
        f.source.publish(pausedSong());
        f.lyricsSource.finish(0, "Private text");
        QVERIFY(!f.metadata().contains("xesam:asText"));
        QVERIFY(!f.exporter()->enabled());
        QVERIFY(f.exporter()->document().isEmpty());
        QVERIFY(f.exporter()->state().isEmpty());
        QCOMPARE(f.exporter()->snapshot().value("version").toUInt(), uint(1));
    }

    void identityGuardBeforeReload_data()
    {
        QTest::addColumn<QString>("identity");
        QTest::newRow("song-key") << QString("key");
        QTest::newRow("new-load") << QString("generation");
        QTest::newRow("new-provider") << QString("provider");
    }

    void identityGuardBeforeReload()
    {
        QFETCH(QString, identity);
        test::UiPlaybackSnapshot source;
        LyricsSource lyricsSource;
        ui::AppSettings settings;
        settings.setExternalLyrics(true);
        ui::Player player(&source, nullptr);
        auto state = pausedSong();
        source.publish(state);
        ui::CurrentLyrics* current = nullptr;
        ui::Mpris* mpris = nullptr;
        bool armed = false, observed = false, oldWasReady = false, textWasEmpty = false;
        QVariantMap guardedDocument, guardedMetadata;
        const auto observe = [&] {
            if (!armed || !current || current->matchesPlayback())
                return;
            observed = true;
            oldWasReady = current->status() == ui::lyrics::Ready;
            textWasEmpty = current->playbackText().isEmpty();
            guardedMetadata = mpris->playerProperties().value("Metadata").toMap();
            // Refresh while the old lyrics still exist, before CurrentLyrics' load slot.
            settings.setExternalLyrics(false);
            settings.setExternalLyrics(true);
            guardedDocument = mpris->findChild<ui::LyricsDBus*>()->document();
        };
        connect(&player, &ui::Player::currentChanged, this, observe);
        connect(&player, &ui::Player::changed, this, observe);
        ui::CurrentLyrics lyrics(&lyricsSource, &player);
        current = &lyrics;
        ui::SingleInstance instance;
        ui::Desktop desktop(&instance);
        ui::Mpris owner(&player, &desktop, &lyrics, &settings);
        mpris = &owner;
        QVERIFY(owner.registered());
        QVERIFY(owner.findChild<ui::LyricsDBus*>());
        lyricsSource.finish(0, "Old private document");
        QVERIFY(lyrics.matchesPlayback());
        QCOMPARE(lyrics.status(), int(ui::lyrics::Ready));
        armed = true;
        if (identity == "key")
            state.current.key = "2";
        else if (identity == "generation")
            ++state.generation;
        else
            state.provider = ui::playback::Netease;
        source.publish(state);
        QVERIFY(observed);
        QVERIFY(oldWasReady);
        QVERIFY(textWasEmpty);
        QVERIFY(!guardedMetadata.contains("xesam:asText"));
        QCOMPARE(guardedDocument.value("status").toString(), QString("loading"));
        QVERIFY(guardedDocument.value("plainText").toString().isEmpty());
        QVERIFY(busList(guardedDocument.value("lines")).isEmpty());
        QCOMPARE(guardedDocument.value("songKey").toString(), state.current.key);
        QCOMPARE(guardedDocument.value("generation").toULongLong(), state.generation);
        QCOMPARE(lyricsSource.replies.size(), 2);
    }

    void lineBoundariesUsePlaybackNotQmlPosition()
    {
        LyricsFixture f;
        QVERIFY(f.mpris.registered());
        QVERIFY(f.exporter());
        f.settings.setExternalLyrics(true);
        auto state = pausedSong();
        f.source.publish(state);
        ui::SongLyrics song;
        song.original.standard = ui::LyricTrack{};
        song.original.standard->offsetMs = 200;
        song.original.standard->lines = {
            {1000, 5000, "Long overlap", {}}, {1500, 2000, "Short overlap", {}},
            {3000, -1, "Ends at blank marker", {}}, {4000, -1, "  ", {}},
            {6000, -1, "Open last line", {}}};
        f.lyricsSource.finish(0, song);
        f.lyrics.setOffset(-200); // Neutralize the file offset for boundary checks.
        f.lyrics.setPositionMs(999999); // Deliberately stale QML position.
        const auto document = f.exporter()->document();
        QCOMPARE(document.value("timing").toString(), QString("lines"));
        const auto lines = document.value("lines").toList();
        QCOMPARE(lines.size(), 4);
        const QList<qint64> starts{1000, 1500, 3000, 6000};
        const QList<qint64> ends{5000, 2000, 4000, -1};
        for (int i = 0; i < lines.size(); ++i) {
            const auto line = lines[i].toMap();
            QCOMPARE(line.value("startMs").toLongLong(), starts[i]);
            QCOMPARE(line.value("endMs").toLongLong(), ends[i]);
            QVERIFY(line.value("words").toList().isEmpty()); // No invented word timestamps.
        }
        struct Boundary { qint64 position; int current; QVariantList active; };
        const QList<Boundary> boundaries{
            {999, -1, {}}, {1000, 0, {0}}, {1499, 0, {0}}, {1500, 1, {0, 1}},
            {2000, 1, {0}}, {2500, 1, {0}}, {3000, 2, {0, 2}}, {4000, 2, {0}},
            {5000, 2, {}}, {5999, 2, {}}, {6000, 3, {3}}, {999, -1, {}}};
        for (const auto& boundary : boundaries) {
            state.positionMs = boundary.position;
            state.positionAt = ui::playback::now();
            f.source.publish(state);
            const auto exported = f.exporter()->state();
            QCOMPARE(exported.value("positionMs").toLongLong(), boundary.position);
            QCOMPARE(exported.value("currentLine").toInt(), boundary.current);
            QCOMPARE(exported.value("activeLines").toList(), boundary.active);
            QVERIFY(exported.value("activeWords").toList().isEmpty());
            QCOMPARE(exported.value("revision"), document.value("revision"));
            QCOMPARE(exported.value("trackId").value<QDBusObjectPath>().path(), f.mpris.trackId());
            QCOMPARE(exported.value("durationMs").toLongLong(), state.durationMs);
            QCOMPARE(exported.value("rate").toDouble(), 0.0);
            QCOMPARE(exported.value("playbackStatus").toString(), QString("paused"));
            QVERIFY(exported.value("sampledAtMs").toLongLong() >= state.positionAt);
        }
        QCOMPARE(f.lyrics.positionMs(), qint64(999999));
        const quint64 revision = document.value("revision").toULongLong();
        f.lyrics.setOffset(300); // +200 file offset +300 user offset = lyrics 500 ms later.
        QCOMPARE(f.exporter()->document().value("shiftMs").toLongLong(), qint64(500));
        QCOMPARE(f.exporter()->document().value("userOffsetMs").toLongLong(), qint64(300));
        QVERIFY(f.exporter()->document().value("revision").toULongLong() > revision);
        state.positionMs = 1499;
        state.positionAt = ui::playback::now();
        f.source.publish(state);
        QCOMPARE(f.exporter()->state().value("currentLine").toInt(), -1);
        state.positionMs = 1500;
        state.positionAt = ui::playback::now();
        f.source.publish(state);
        QCOMPARE(f.exporter()->state().value("activeLines").toList(), QVariantList{0});
        state.previewStartMs = 1000;
        state.positionMs = 500;
        state.positionAt = ui::playback::now();
        f.source.publish(state);
        QCOMPARE(f.exporter()->document().value("previewStartMs").toLongLong(), qint64(1000));
        QCOMPARE(f.exporter()->document().value("shiftMs").toLongLong(), qint64(-500));
        QCOMPARE(f.exporter()->state().value("currentLine").toInt(), 0);
        QCOMPARE(f.exporter()->document().value("lines").toList().first().toMap()
                     .value("startMs").toLongLong(), qint64(1000));
    }

    void wordsLanguagesAndBusMarshaling()
    {
        LyricsFixture f;
        QVERIFY(f.mpris.registered());
        QVERIFY(f.exporter());
        f.settings.setExternalLyrics(true);
        auto state = pausedSong();
        state.positionMs = 1100;
        f.source.publish(state);
        const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
        const QString text = "A" + emoji + " B";
        ui::SongLyrics song = lineLyrics("Ignored standard track");
        song.original.enhanced = ui::LyricTrack{};
        song.original.enhanced->timing = ui::LyricTrack::Words;
        song.original.enhanced->lines = {{1000, 1600, text,
            {{1000, 1100, "A"}, {1100, 1450, emoji}, {1450, 1500, "B"}}}};
        song.translation.standard = ui::LyricTrack{};
        song.translation.standard->offsetMs = 100;
        song.translation.standard->lines = {{0, 500, " ", {}},
                                            {900, 1500, " Translated ", {}}};
        song.romanization.standard = ui::LyricTrack{};
        song.romanization.standard->offsetMs = -50;
        song.romanization.standard->lines = {{1050, 1650, " Romanized ", {}}};
        f.lyricsSource.finish(0, song);
        QCOMPARE(f.metadata().value("xesam:asText").toString(), text);
        const auto reply = call(kLyricsPath, kLyricsInterface, "GetSnapshot");
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.signature(), QString("a{sv}"));
        const auto snapshot = busMap(reply.arguments().value(0));
        QVERIFY(snapshot.value("enabled").toBool());
        QCOMPARE(snapshot.value("version").metaType(), QMetaType::fromType<uint>());
        const auto document = busMap(snapshot.value("document"));
        const auto exportedState = busMap(snapshot.value("state"));
        QCOMPARE(document.value("revision").metaType(), QMetaType::fromType<quint64>());
        QCOMPARE(document.value("generation").metaType(), QMetaType::fromType<quint64>());
        QCOMPARE(document.value("trackId").metaType(), QMetaType::fromType<QDBusObjectPath>());
        QCOMPARE(document.value("trackId").value<QDBusObjectPath>().path(), f.mpris.trackId());
        QCOMPARE(document.value("songKey").toString(), QString("1"));
        QCOMPARE(document.value("playbackProvider").toString(), QString("bilibili"));
        QCOMPARE(document.value("status").toString(), QString("ready"));
        QCOMPARE(document.value("timing").toString(), QString("words"));
        QCOMPARE(document.value("plainText").toString(), text);
        for (const QString& key : {QString("shiftMs"), QString("userOffsetMs"),
                                   QString("previewStartMs")})
            QCOMPARE(document.value(key).metaType(), QMetaType::fromType<qlonglong>());
        const auto lines = busList(document.value("lines"));
        QCOMPARE(lines.size(), 1);
        const auto line = busMap(lines.first());
        QCOMPARE(line.value("text").toString(), text);
        QCOMPARE(line.value("translation").toString(), QString("Translated"));
        QCOMPARE(line.value("romanization").toString(), QString("Romanized"));
        QCOMPARE(line.value("startMs").metaType(), QMetaType::fromType<qlonglong>());
        QCOMPARE(line.value("endMs").metaType(), QMetaType::fromType<qlonglong>());
        QCOMPARE(line.value("startMs").toLongLong(), qint64(1000));
        QCOMPARE(line.value("endMs").toLongLong(), qint64(1600));
        const auto words = busList(line.value("words"));
        QCOMPARE(words.size(), 3);
        const QList<int> from{0, 1, 4}, to{1, 3, 5}; // UTF-16 units, not code points/UTF-8 bytes.
        for (int i = 0; i < words.size(); ++i) {
            const auto word = busMap(words[i]);
            const auto original = song.original.enhanced->lines.first().words[i];
            QCOMPARE(word.value("text").toString(), original.text);
            QCOMPARE(word.value("from").metaType(), QMetaType::fromType<int>());
            QCOMPARE(word.value("to").metaType(), QMetaType::fromType<int>());
            QCOMPARE(word.value("from").toInt(), from[i]);
            QCOMPARE(word.value("to").toInt(), to[i]);
            QCOMPARE(text.mid(from[i], to[i] - from[i]), original.text);
            QCOMPARE(word.value("startMs").metaType(), QMetaType::fromType<qlonglong>());
            QCOMPARE(word.value("endMs").metaType(), QMetaType::fromType<qlonglong>());
            QCOMPARE(word.value("durationMs").metaType(), QMetaType::fromType<qlonglong>());
            QCOMPARE(word.value("startMs").toLongLong(), original.startMs);
            QCOMPARE(word.value("endMs").toLongLong(), original.endMs);
            QCOMPARE(word.value("durationMs").toLongLong(), original.endMs - original.startMs);
        }
        QCOMPARE(exportedState.value("revision"), document.value("revision"));
        QCOMPARE(exportedState.value("trackId"), document.value("trackId"));
        QCOMPARE(exportedState.value("positionMs").toLongLong(), qint64(1100));
        for (const QString& key : {QString("positionMs"), QString("sampledAtMs"), QString("durationMs")})
            QCOMPARE(exportedState.value(key).metaType(), QMetaType::fromType<qlonglong>());
        QCOMPARE(exportedState.value("durationMs").toLongLong(), state.durationMs);
        QVERIFY(exportedState.value("sampledAtMs").toLongLong() >= state.positionAt);
        QCOMPARE(exportedState.value("rate").metaType(), QMetaType::fromType<double>());
        QCOMPARE(exportedState.value("rate").toDouble(), 0.0);
        QCOMPARE(exportedState.value("playbackStatus").toString(), QString("paused"));
        QCOMPARE(exportedState.value("currentLine").metaType(), QMetaType::fromType<int>());
        const auto busActiveLines = busList(exportedState.value("activeLines"));
        QCOMPARE(busActiveLines.size(), 1);
        QCOMPARE(unbox(busActiveLines.first()).toInt(), 0);
        const auto active = busList(exportedState.value("activeWords"));
        QCOMPARE(active.size(), 1);
        QCOMPARE(busMap(active.first()).value("line").toInt(), 0);
        QCOMPARE(busMap(active.first()).value("word").toInt(), 1);
        for (const qint64 position : {qint64(1000), qint64(1450), qint64(1500), qint64(1100)}) {
            state.positionMs = position;
            state.positionAt = ui::playback::now();
            f.source.publish(state);
            const auto activeWords = f.exporter()->state().value("activeWords").toList();
            if (position == 1500) {
                QVERIFY(activeWords.isEmpty());
                QCOMPARE(f.exporter()->state().value("activeLines").toList(), QVariantList{0});
            } else {
                QCOMPARE(activeWords.size(), 1);
                QCOMPARE(activeWords.first().toMap().value("word").toInt(),
                         position == 1000 ? 0 : position == 1450 ? 2 : 1);
            }
        }

        const auto propertiesReply = call(kLyricsPath, kPropertiesInterface, "GetAll",
                                          {kLyricsInterface});
        QCOMPARE(propertiesReply.type(), QDBusMessage::ReplyMessage);
        const auto properties = busMap(propertiesReply.arguments().value(0));
        QCOMPARE(properties.value("Version").metaType(), QMetaType::fromType<uint>());
        QCOMPARE(properties.value("Version").toUInt(), uint(1));
        QCOMPARE(properties.value("Enabled").metaType(), QMetaType::fromType<bool>());
        QVERIFY(properties.value("Enabled").toBool());
        QCOMPARE(busMap(properties.value("Document")).value("revision"), document.value("revision"));
        QCOMPARE(busMap(properties.value("State")).value("revision"), document.value("revision"));
        const auto introspection = call(kLyricsPath, "org.freedesktop.DBus.Introspectable", "Introspect");
        QCOMPARE(introspection.type(), QDBusMessage::ReplyMessage);
        QXmlStreamReader xml(introspection.arguments().value(0).toString());
        bool inInterface = false, inSnapshot = false, snapshotOutput = false;
        QMap<QString, QString> types;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"interface")
                inInterface = xml.attributes().value("name") == kLyricsInterface;
            else if (xml.isEndElement() && xml.name() == u"interface")
                inInterface = false;
            if (!inInterface)
                continue;
            if (xml.isStartElement() && xml.name() == u"property") {
                types.insert(xml.attributes().value("name").toString(),
                             xml.attributes().value("type").toString());
                QCOMPARE(xml.attributes().value("access").toString(), QString("read"));
            }
            if (xml.isStartElement() && xml.name() == u"method")
                inSnapshot = xml.attributes().value("name") == u"GetSnapshot";
            if (xml.isEndElement() && xml.name() == u"method")
                inSnapshot = false;
            if (inSnapshot && xml.isStartElement() && xml.name() == u"arg" &&
                xml.attributes().value("direction") == u"out")
                snapshotOutput = xml.attributes().value("type") == u"a{sv}";
        }
        QVERIFY(!xml.hasError());
        QCOMPARE(types.value("Version"), QString("u"));
        QCOMPARE(types.value("Enabled"), QString("b"));
        QCOMPARE(types.value("Document"), QString("a{sv}"));
        QCOMPARE(types.value("State"), QString("a{sv}"));
        QVERIFY(snapshotOutput);
    }

    void plainAbsentAndFailedLyrics_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("status");
        QTest::newRow("plain") << QString("plain") << QString("ready");
        QTest::newRow("no-lyrics") << QString("none") << QString("empty");
        QTest::newRow("instrumental") << QString("instrumental") << QString("instrumental");
        QTest::newRow("failed") << QString("failed") << QString("error");
        QTest::newRow("unaligned-words-fall-back") << QString("fallback") << QString("ready");
    }

    void plainAbsentAndFailedLyrics()
    {
        QFETCH(QString, kind);
        QFETCH(QString, status);
        LyricsFixture f;
        QVERIFY(f.mpris.registered());
        QVERIFY(f.exporter());
        f.settings.setExternalLyrics(true);
        auto state = pausedSong();
        f.source.publish(state);
        f.lyricsSource.finish(0, "Previous lyrics");
        ++state.generation;
        f.source.publish(state);
        QVERIFY(!f.metadata().contains("xesam:asText"));
        ui::SongLyrics song;
        if (kind == "plain") {
            song.original.standard = ui::LyricTrack{};
            song.original.standard->timing = ui::LyricTrack::PlainText;
            song.original.standard->lines = {{-1, -1, "Plain first", {}}, {-1, -1, "Plain second", {}}};
            song.translation.standard = ui::LyricTrack{};
            song.translation.standard->timing = ui::LyricTrack::PlainText;
            song.translation.standard->lines = {{-1, -1, " ", {}},
                {-1, -1, " Translation first ", {}}, {-1, -1, "Translation second", {}}};
            song.romanization.standard = ui::LyricTrack{};
            song.romanization.standard->timing = ui::LyricTrack::PlainText;
            song.romanization.standard->lines = {{-1, -1, "Roman first", {}},
                                                {-1, -1, "Roman second", {}}};
        } else if (kind == "fallback") {
            song.original.standard = ui::LyricTrack{};
            song.original.standard->timing = ui::LyricTrack::Words;
            song.original.standard->lines = {{1000, 2000, "Only a line", {{1000, 1500, "Missing"}}}};
        } else {
            song.noLyric = kind == "none";
            song.instrumental = kind == "instrumental";
        }
        if (kind == "failed")
            f.lyricsSource.finishOutcome(1, {{}, false, ui::Failure{ui::failure::Rejected, {}}});
        else
            f.lyricsSource.finish(1, song);
        const auto document = f.exporter()->document();
        QCOMPARE(document.value("status").toString(), status);
        if (kind == "plain") {
            QCOMPARE(document.value("timing").toString(), QString("plain"));
            QCOMPARE(document.value("plainText").toString(), QString("Plain first\nPlain second"));
            QCOMPARE(f.metadata().value("xesam:asText"), document.value("plainText"));
            const auto lines = document.value("lines").toList();
            QCOMPARE(lines.size(), 2);
            QCOMPARE(lines[0].toMap().value("translation").toString(), QString("Translation first"));
            QCOMPARE(lines[1].toMap().value("translation").toString(), QString("Translation second"));
            QCOMPARE(lines[0].toMap().value("romanization").toString(), QString("Roman first"));
            QCOMPARE(lines[1].toMap().value("romanization").toString(), QString("Roman second"));
            for (const auto& value : lines) {
                const auto line = value.toMap();
                QCOMPARE(line.value("startMs").toLongLong(), qint64(-1));
                QCOMPARE(line.value("endMs").toLongLong(), qint64(-1));
                QVERIFY(line.value("words").toList().isEmpty());
            }
            QCOMPARE(f.exporter()->state().value("currentLine").toInt(), 0);
        } else if (kind == "fallback") {
            QCOMPARE(document.value("timing").toString(), QString("lines"));
            const auto line = document.value("lines").toList().first().toMap();
            QCOMPARE(line.value("startMs").toLongLong(), qint64(1000));
            QCOMPARE(line.value("endMs").toLongLong(), qint64(2000));
            QVERIFY(line.value("words").toList().isEmpty());
        } else {
            QVERIFY(document.value("lines").toList().isEmpty());
            QVERIFY(document.value("plainText").toString().isEmpty());
            QVERIFY(!f.metadata().contains("xesam:asText"));
            QCOMPARE(f.exporter()->state().value("currentLine").toInt(), -1);
        }
        QVERIFY(f.exporter()->state().value("activeLines").toList().isEmpty());
        QVERIFY(f.exporter()->state().value("activeWords").toList().isEmpty());
        QCOMPARE(f.metadata().value("xesam:title").toString(), QString("Song"));
        if (kind == "failed") {
            f.lyrics.retry();
            QCOMPARE(f.lyricsSource.replies.size(), 3);
            QCOMPARE(f.exporter()->document().value("status").toString(), QString("loading"));
            f.lyricsSource.finish(2, "Recovered");
            QCOMPARE(f.exporter()->document().value("status").toString(), QString("ready"));
        }
    }

    void resumedPlaybackPublishesBoundariesWithoutQml()
    {
        LyricsFixture f;
        QVERIFY(f.mpris.registered());
        f.settings.setExternalLyrics(true);
        auto state = pausedSong();
        f.source.publish(state);
        ui::SongLyrics song;
        song.original.standard = ui::LyricTrack{};
        song.original.standard->lines = {{100, 1000, "Timer line", {}}};
        f.lyricsSource.finish(0, song); // document became ready with a stopped clock
        QSignalSpy events(this, &MprisLyricsTest::lyricsPropertiesSeen);
        state.status = ui::playback::Playing;
        state.positionAt = ui::playback::now();
        state.rate = 1;
        f.source.publish(state); // no more source position updates after this
        const auto boundarySeen = [&] {
            for (const auto& event : events) {
                const auto clock = busMap(event.at(1).toMap().value("State"));
                if (clock.value("currentLine").toInt() == 0 &&
                    clock.value("positionMs").toLongLong() >= 100)
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(boundarySeen(), 1500);
    }

    void standardCommandsRemainAvailable()
    {
        CommandSource source;
        ui::AppSettings settings;
        ui::Player player(&source, nullptr);
        LyricsSource lyricsSource;
        ui::CurrentLyrics lyrics(&lyricsSource, &player);
        ui::SingleInstance instance;
        ui::Desktop desktop(&instance);
        ui::Mpris mpris(&player, &desktop, &lyrics, &settings);
        QVERIFY(mpris.registered());
        lyricsSource.finish(0, "Private lyrics");
        for (const bool enabled : {false, true, false}) {
            settings.setExternalLyrics(enabled);
            const auto properties = mpris.playerProperties();
            for (const QString& key : {QString("CanControl"), QString("CanPlay"), QString("CanPause"),
                                       QString("CanSeek"), QString("CanGoNext"), QString("CanGoPrevious")})
                QVERIFY(properties.value(key).toBool());
            QCOMPARE(properties.value("Metadata").toMap().value("xesam:title").toString(), QString("Song"));
            for (const QString& command : {QString("PlayPause"), QString("Next"), QString("Previous")})
                QCOMPARE(call(kPlayerPath, kPlayerInterface, command).type(), QDBusMessage::ReplyMessage);
            QCOMPARE(call(kPlayerPath, kPlayerInterface, "Seek", {qlonglong(1500000)}).type(),
                     QDBusMessage::ReplyMessage);
            QCOMPARE(call(kPlayerPath, kPlayerInterface, "SetPosition",
                          {QVariant::fromValue(QDBusObjectPath(mpris.trackId())), qlonglong(2500000)}).type(),
                     QDBusMessage::ReplyMessage);
            QCOMPARE(call(kPlayerPath, kPropertiesInterface, "Set",
                          {kPlayerInterface, QString("Volume"), QVariant::fromValue(QDBusVariant(0.25))}).type(),
                     QDBusMessage::ReplyMessage);
            QCOMPARE(call(kPlayerPath, kPropertiesInterface, "Set",
                          {kPlayerInterface, QString("Shuffle"), QVariant::fromValue(QDBusVariant(true))}).type(),
                     QDBusMessage::ReplyMessage);
            QCOMPARE(call(kPlayerPath, kPropertiesInterface, "Set",
                          {kPlayerInterface, QString("LoopStatus"),
                           QVariant::fromValue(QDBusVariant(QString("Track")))}).type(),
                     QDBusMessage::ReplyMessage);
        }
        QCOMPARE(source.toggles, 3);
        QCOMPARE(source.nextCalls, 3);
        QCOMPARE(source.previousCalls, 3);
        QCOMPARE(source.seeks, QList<qint64>({1500, 2500, 1500, 2500, 1500, 2500}));
        QCOMPARE(source.volume, 0.25);
        QCOMPARE(source.order, ui::playback::Shuffle);
        QCOMPARE(source.repeat, ui::playback::RepeatOne);
    }

    void documentsAreValidatedAndBounded_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<int>("count");
        QTest::addColumn<bool>("valid");
        QTest::newRow("4096-lines") << QString("lines") << 4096 << true;
        QTest::newRow("4097-lines") << QString("lines") << 4097 << false;
        QTest::newRow("32768-words") << QString("words") << 32768 << true;
        QTest::newRow("32769-words") << QString("words") << 32769 << false;
        QTest::newRow("512Ki-utf16") << QString("text") << 512 * 1024 << true;
        QTest::newRow("over-512Ki-utf16") << QString("text") << 512 * 1024 + 1 << false;
        QTest::newRow("aggregate-languages-limit") << QString("languages") << 256 * 1024 << true;
        QTest::newRow("aggregate-languages-over-limit") << QString("languages") << 256 * 1024 + 1 << false;
        QTest::newRow("oversized-romanization") << QString("romanization") << 512 * 1024 << false;
        for (const char* kind : {"null", "unpaired-surrogate", "invalid-translation",
                                 "negative-line", "reversed-line", "negative-word", "reversed-word"})
            QTest::newRow(kind) << QString::fromLatin1(kind) << 0 << false;
    }

    void documentsAreValidatedAndBounded()
    {
        QFETCH(QString, kind);
        QFETCH(int, count);
        QFETCH(bool, valid);
        LyricsFixture f;
        QVERIFY(f.mpris.registered());
        QVERIFY(f.exporter());
        f.settings.setExternalLyrics(true);
        auto state = pausedSong();
        f.source.publish(state);
        f.lyricsSource.finish(0, "Previous valid document");
        const auto previousRevision = f.exporter()->document().value("revision").toULongLong();
        ++state.generation;
        state.positionMs = 0; // Keep the word-limit fixture out of its 32768 simultaneous words.
        f.source.publish(state);
        auto song = lineLyrics("x");
        auto& track = *song.original.standard;
        if (kind == "lines") {
            track.lines.clear();
            for (int i = 0; i < count; ++i)
                track.lines.append({i, i + 1, "x", {}});
        } else if (kind == "words") {
            track.timing = ui::LyricTrack::Words;
            track.lines.first().text = QString(count, QLatin1Char('a'));
            for (int i = 0; i < count; ++i)
                track.lines.first().words.append({1000, 1100, "a"});
        } else if (kind == "text" || kind == "languages") {
            track.lines.first().text = QString(count, QLatin1Char('x'));
            if (kind == "languages") {
                song.translation.standard = ui::LyricTrack{};
                song.translation.standard->lines = {{1000, 2000, QString(256 * 1024, QLatin1Char('t')), {}}};
            }
        } else if (kind == "romanization") {
            song.romanization.standard = ui::LyricTrack{};
            song.romanization.standard->lines = {{1000, 2000, QString(count, QLatin1Char('r')), {}}};
        } else if (kind == "null") {
            track.lines.first().text = QString("a") + QChar(QChar::Null) + "b";
        } else if (kind == "unpaired-surrogate") {
            track.lines.first().text = QString(QChar(0xd800));
        } else if (kind == "invalid-translation") {
            song.translation.standard = ui::LyricTrack{};
            song.translation.standard->lines = {{1000, 2000, QString(QChar(0xd800)), {}}};
        } else if (kind == "negative-line") {
            track.lines.first().startMs = -2;
        } else if (kind == "reversed-line") {
            track.lines.first().endMs = 900;
        } else {
            track.timing = ui::LyricTrack::Words;
            track.lines.first().words = {{kind == "negative-word" ? -1 : 1100, 1000, "x"}};
        }
        f.lyricsSource.finish(1, song);
        const auto document = f.exporter()->document();
        QVERIFY(document.value("revision").toULongLong() > previousRevision);
        QCOMPARE(document.value("generation").toULongLong(), state.generation);
        QCOMPARE(document.value("status").toString(), valid ? QString("ready") : QString("error"));
        const auto lines = document.value("lines").toList();
        if (valid) {
            QVERIFY(!document.contains("errorCode"));
            QCOMPARE(lines.size(), kind == "lines" ? count : 1);
            if (kind == "words")
                QCOMPARE(lines.first().toMap().value("words").toList().size(), count);
            if (kind == "text" || kind == "languages")
                QCOMPARE(document.value("plainText").toString().size(), qsizetype(count));
        } else {
            QCOMPARE(document.value("errorCode").toString(), QString("invalid-document"));
            QVERIFY(lines.isEmpty()); // Reject, never truncate a timed/UTF-16 sequence.
            QVERIFY(document.value("plainText").toString().isEmpty());
            QCOMPARE(f.exporter()->state().value("currentLine").toInt(), -1);
            QVERIFY(f.exporter()->state().value("activeLines").toList().isEmpty());
            QVERIFY(f.exporter()->state().value("activeWords").toList().isEmpty());
        }
        QCOMPARE(f.metadata().value("xesam:title").toString(), QString("Song"));
        if (kind == "null" || kind == "unpaired-surrogate" || (kind == "text" && !valid))
            QVERIFY(!f.metadata().contains("xesam:asText"));
    }
};

QTEST_MAIN(MprisLyricsTest)
#include "mpris_lyrics_test.moc"
