#include "../support/fake_range_http.h"
#include "infrastructure/media/audio_engine.h"
#include "infrastructure/media/audio_store.h"
#include "infrastructure/media/automix/analysis.h"
#include <QAudioDevice>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QMediaDevices>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_LINUX
#include "../support/private_pulse_audio.h"
#endif
using namespace cirrus;
using namespace domain;
using application::PlaybackEvent;
using Kind = PlaybackEvent::Kind;
using media::AudioEngine;
using media::AudioStore;
namespace automix = media::automix;
namespace
{
QByteArray fixture(const QString& name)
{
    QFile file(QStringLiteral(AUDIO_FIXTURES "/") + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
MediaSource source(const QByteArray& body, const QString& format,
                   const QString& path = QStringLiteral("/tone"))
{
    MediaSource s{QUrl(QStringLiteral("https://m801.music.126.net") + path), "standard", format,
                  body.size(), QDeadlineTimer(60000)};
    s.identity = ContentHash{QCryptographicHash::hash(body, QCryptographicHash::Md5).toHex()};
    return s;
}
LoadToken token(quint64 n) { return LoadToken{{QUuid::createUuid(), 1}, n}; }
QList<Kind> kinds(const QList<PlaybackEvent>& events)
{
    QList<Kind> list;
    for (const auto& e : events)
        list.append(e.kind);
    return list;
}
} // namespace

// Linux owns a private null sink; other platforms need a default output.
// All cases play at volume 0.
class AudioEngineTest : public QObject
{
    Q_OBJECT
#ifdef Q_OS_LINUX
    test::PrivatePulseAudioServer m_pulse;
#endif
    std::unique_ptr<QTemporaryDir> m_dir;
    test::RangeServer m_server;
    std::unique_ptr<AudioStore> m_store;
    std::unique_ptr<AudioEngine> m_engine;
    QList<PlaybackEvent> m_events;

    void load(const QString& file, const QString& format, LoadToken t)
    {
        m_server.body = fixture(file);
        m_engine->load(source(m_server.body, format), t);
    }
    bool has(Kind kind) const
    {
        return std::any_of(m_events.begin(), m_events.end(),
                           [kind](const PlaybackEvent& e) { return e.kind == kind; });
    }
    const PlaybackEvent& first(Kind kind) const
    {
        return *std::find_if(m_events.begin(), m_events.end(),
                             [kind](const PlaybackEvent& e) { return e.kind == kind; });
    }
    qint64 lastPosition(LoadToken t) const
    {
        qint64 last = -1;
        for (const auto& e : m_events)
            if (e.token == t && e.kind == Kind::Position)
                last = e.value;
        return last;
    }
    // long.mp3 with another file preloaded, playing until it is inside their transition
    // (about 2.7 s to the end; the next becomes current at about 3.4 s).
    bool playIntoTheTransition(LoadToken current, LoadToken next)
    {
        load(QStringLiteral("long.mp3"), QStringLiteral("mp3"), current);
        auto other = source(m_server.body, QStringLiteral("mp3"));
        other.identity = ContentHash{}; // another file to the engine (the fake CDN has one body)
        m_engine->preload(other, next);
        if (!QTest::qWaitFor([this] { return has(Kind::Ready); }, 5000))
            return false;
        m_engine->play();
        return QTest::qWaitFor([&] { return lastPosition(current) >= 2850; }, 10000) &&
               !has(Kind::Advanced);
    }

  private slots:
    void initTestCase()
    {
#ifdef Q_OS_LINUX
        QVERIFY2(m_pulse.start(), qPrintable(m_pulse.errorString()));
#endif
    }

    void cleanupTestCase()
    {
#ifdef Q_OS_LINUX
        QVERIFY2(m_pulse.stop(), "Could not reap this test's private PulseAudio server");
#endif
    }

    void init()
    {
        const auto output = QMediaDevices::defaultAudioOutput();
        const bool hasOutput = !output.isNull();
#ifdef Q_OS_LINUX
        QVERIFY2(hasOutput, "Private PulseAudio output disappeared");
#else
        QVERIFY2(hasOutput, "Native audio tests require a default system audio output device");
#endif
#ifdef Q_OS_WIN
        const auto expectedEndpoint = qgetenv("CIRRUS_TEST_AUDIO_ENDPOINT_ID");
        if (qEnvironmentVariableIsSet("GITHUB_ACTIONS"))
            QVERIFY2(!expectedEndpoint.isEmpty(), "CI must provision the trusted native audio endpoint");
        if (!expectedEndpoint.isEmpty()) {
            qInfo() << "Qt default audio output:" << output.id() << output.description();
            QVERIFY2(output.isDefault(), "Qt must identify the system default output");
            QCOMPARE(output.id().toLower(), expectedEndpoint.toLower());
        }
#endif
        m_dir = std::make_unique<QTemporaryDir>(QCoreApplication::applicationDirPath() +
                                                QStringLiteral("/audio-engine-XXXXXX"));
        AudioStore::Options options;
        options.root = m_dir->path() + QStringLiteral("/audio");
        m_store =
            std::make_unique<AudioStore>(options, std::make_unique<test::RangeManager>(m_server));
        m_store->setPartition(QStringLiteral("test"));
        m_engine = std::make_unique<AudioEngine>(*m_store);
        m_engine->setVolume(0);
        m_events.clear();
        connect(m_engine.get(), &AudioEngine::event, this,
                [this](PlaybackEvent e) { m_events.append(e); });
    }
    void cleanup()
    {
        m_engine.reset();
        m_store.reset();
        m_dir.reset();
    }

    void opensPlaysAndEnds()
    {
        const auto t = token(1);
        QList<PlaybackEvent> openingEvents;
        QObject readyObserver;
        connect(
            m_engine.get(), &AudioEngine::event, &readyObserver,
            [this, &openingEvents](PlaybackEvent e) {
                if (e.kind == Kind::Ready && openingEvents.isEmpty())
                    openingEvents = m_events;
            },
            Qt::DirectConnection);
        load(QStringLiteral("tone.flac"), QStringLiteral("flac"), t);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        // Capture the exact opening sequence before a later timer tick can append events.
        QCOMPARE(kinds(openingEvents),
                 (QList<Kind>{Kind::Duration, Kind::Seekable, Kind::Volume, Kind::Ready}));
        QVERIFY(std::abs(openingEvents[0].value - 500) <= 30);
        QCOMPARE(openingEvents[1].value, 1);
        QCOMPARE(openingEvents[2].volumeLinear, std::optional(0.f));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Observation), 5000);
        QCOMPARE(kinds(m_events), (QList<Kind>{Kind::Duration, Kind::Seekable, Kind::Volume,
                                               Kind::Ready, Kind::Position, Kind::Observation}));
        QCOMPARE(first(Kind::Position).value, 0);
        QVERIFY(first(Kind::Observation).observed.has_value());
        QCOMPARE(first(Kind::Observation).observed->playedMs, 0);
        QVERIFY(!first(Kind::Observation).observed->capped);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 5000);
        qint64 last = -1;
        bool playing = false;
        for (const auto& e : std::as_const(m_events)) {
            QCOMPARE(e.token, t);
            if (e.kind == Kind::State && e.state == PlaybackState::Playing)
                playing = true;
            if (e.kind == Kind::Position) {
                QVERIFY(e.value > last); // forwards only
                QVERIFY(e.value <= 500);
                last = e.value;
            }
        }
        QVERIFY(playing);
        QVERIFY(last > 0);
    }

    void seekIsConfirmedAtTheTarget()
    {
        load(QStringLiteral("tone.mp3"), QStringLiteral("mp3"), token(2));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->pause();
        m_engine->seek(300);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::SeekConfirmed), 5000);
        const auto confirmed =
            std::find_if(m_events.begin(), m_events.end(),
                         [](const PlaybackEvent& e) { return e.kind == Kind::SeekConfirmed; });
        QCOMPARE(confirmed->value, 300);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 5000);
    }

    // A seek past what has downloaded (2026-11-22, the maintainer: the bar sprang back, and
    // went on doing so for a while). The engine wakes a read waiting on the download
    // (AudioStream::interrupt); with none waiting, the seek's own read took that wake-up:
    // the seek was dropped or never got there, and the download was not sent there.
    void aSeekPastTheDownloadPlaysFromThere()
    {
        // seek.mp3 (200 KB, 200 s; opening reads 64 KB and the end) in 8 KB chunks, as a
        // song of some MB in 256 KB ones: the first 120 KB at once, the rest slowly (still
        // faster than it plays). Paused, the producer fills its ring and waits there, in
        // no read; 180 s is past the download.
        m_engine.reset();
        AudioStore::Options options;
        options.root = m_dir->path() + QStringLiteral("/chunked");
        options.chunkBytes = 8192;
        options.aheadBytes = 8192;
        m_store =
            std::make_unique<AudioStore>(options, std::make_unique<test::RangeManager>(m_server));
        m_store->setPartition(QStringLiteral("test"));
        m_engine = std::make_unique<AudioEngine>(*m_store);
        m_engine->setVolume(0);
        connect(m_engine.get(), &AudioEngine::event, this,
                [this](PlaybackEvent e) { m_events.append(e); });
        m_server.blockBytes = 1024;
        m_server.slowAfter = 120000;
        m_server.slowIntervalMs = 400;
        const auto t = token(3);
        load(QStringLiteral("seek.mp3"), QStringLiteral("mp3"), t);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 8000);
        m_engine->pause();
        QTest::qWait(300);
        m_engine->seek(180000);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::SeekConfirmed), 3000);
        QCOMPARE(first(Kind::SeekConfirmed).value, 180000);
        // The download went there (the chunk of byte 180 000; opening asked for the end's),
        // and it plays on from there.
        QVERIFY(std::any_of(m_server.ranges.cbegin(), m_server.ranges.cend(), [](auto& range) {
            const qint64 from = range.mid(6, range.indexOf('-') - 6).toLongLong();
            return from >= 160000 && from < 190000;
        }));
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(lastPosition(t) >= 181000, 5000);
    }
    void stopRetiresEverything()
    {
        load(QStringLiteral("tone.flac"), QStringLiteral("flac"), token(3));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->play();
        m_engine->stop();
        const auto count = m_events.size();
        QTest::qWait(400);
        QCOMPARE(m_events.size(), count); // no completion events, not even queued ones
    }

    void aWrongFormatFails()
    {
        load(QStringLiteral("tone.flac"), QStringLiteral("mp3"), token(4));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Failure), 5000);
        QCOMPARE(m_events.last().failure.error, PlaybackError::UnsupportedFormat);
        QVERIFY(!has(Kind::Ready));
    }

    // The preloaded track follows without Ended: Advanced, then its Duration and Seekable.
    void aPreloadedTrackPlaysOn()
    {
        const auto first = token(7), second = token(8);
        load(QStringLiteral("tone.flac"), QStringLiteral("flac"), first);
        m_engine->preload(source(m_server.body, QStringLiteral("flac")), second);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 5000);
        const auto advanced =
            std::find_if(m_events.begin(), m_events.end(),
                         [](const PlaybackEvent& e) { return e.kind == Kind::Advanced; });
        QVERIFY(advanced != m_events.end());
        QCOMPARE(advanced->token, second);
        for (auto it = m_events.begin(); it != advanced; ++it) {
            QCOMPARE(it->token, first);
            QVERIFY(it->kind != Kind::Ended);
        }
        QCOMPARE((advanced + 1)->kind, Kind::Duration);
        QVERIFY(std::abs((advanced + 1)->value - 500) <= 30);
        QCOMPARE((advanced + 2)->kind, Kind::Seekable);
        qint64 last = -1;
        for (auto it = advanced; it != m_events.end(); ++it) {
            QCOMPARE(it->token, second);
            if (it->kind == Kind::Position) {
                QVERIFY(it->value > last && it->value <= 500); // its own clock, from 0
                last = it->value;
            }
        }
        QCOMPARE(m_events.last().kind, Kind::Ended);
    }

    // The producer is on the next track long before playback (0.5 s files); a seek in the
    // current one decodes it again.
    void aSeekBeforeTheJointStaysOnTheTrack()
    {
        const auto first = token(9), second = token(10);
        load(QStringLiteral("tone.mp3"), QStringLiteral("mp3"), first);
        m_engine->preload(source(m_server.body, QStringLiteral("mp3")), second);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->pause();
        QTest::qWait(300);
        m_engine->seek(200);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::SeekConfirmed), 5000);
        const auto confirmed =
            std::find_if(m_events.begin(), m_events.end(),
                         [](const PlaybackEvent& e) { return e.kind == Kind::SeekConfirmed; });
        QCOMPARE(confirmed->token, first);
        QCOMPARE(confirmed->value, 200);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 5000);
        QVERIFY(has(Kind::Advanced));
        QCOMPARE(m_events.last().token, second);
    }

    void aDroppedPreloadEndsNormally()
    {
        const auto first = token(11), second = token(12);
        load(QStringLiteral("tone.flac"), QStringLiteral("flac"), first);
        m_engine->preload(source(m_server.body, QStringLiteral("flac")), second);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        QTest::qWait(300);
        m_engine->preload({}, {});
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 5000);
        QVERIFY(!has(Kind::Advanced));
        for (const auto& e : std::as_const(m_events))
            QCOMPARE(e.token, first);
    }

    // Smart transitions (docs/tasks/automix.md): long enough tracks overlap, and the next
    // becomes current at the switch point, already some way in.
    void aTransitionMixesTheNextIn()
    {
        const auto first = token(13), second = token(14);
        load(QStringLiteral("long.mp3"), QStringLiteral("mp3"), first);
        auto next = source(m_server.body, QStringLiteral("mp3"));
        next.identity = ContentHash{}; // another file to the engine (the fake CDN has one body)
        m_engine->preload(next, second);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Advanced), 10000);
        const auto advanced = std::find_if(m_events.cbegin(), m_events.cend(),
                                           [](const auto& e) { return e.kind == Kind::Advanced; });
        // Event delivery may reallocate the list while QTRY processes the next tick.
        const auto advancedIndex = advanced - m_events.cbegin();
        qint64 lastOfFirst = -1;
        for (auto it = m_events.cbegin(); it != advanced; ++it)
            if (it->kind == Kind::Position)
                lastOfFirst = it->value;
        // 4 s tracks: a third of one (about 1.36 s) overlaps; the switch is in its middle.
        QVERIFY2(lastOfFirst > 2400 && lastOfFirst < 3700,
                 qPrintable(QString::number(lastOfFirst)));
        QTRY_VERIFY_WITH_TIMEOUT(
            std::any_of(m_events.cbegin() + advancedIndex, m_events.cend(),
                        [](const auto& e) { return e.kind == Kind::Position; }),
            2000);
        const auto firstOfSecond =
            std::find_if(m_events.begin(), m_events.end(), [&](const auto& e) {
                return e.token == second && e.kind == Kind::Position;
            });
        QVERIFY(firstOfSecond != m_events.end());
        QVERIFY2(firstOfSecond->value >= 450 && firstOfSecond->value < 1200,
                 qPrintable(QString::number(firstOfSecond->value)));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 10000);
        QCOMPARE(m_events.last().token, second);
    }

    void aSeekInTheTransitionStaysOnTheTrack()
    {
        // Before the switch the current track is still the one shown: the seek is in it,
        // and the transition comes again.
        const auto one = token(17), two = token(18);
        QVERIFY(playIntoTheTransition(one, two));
        m_engine->seek(1000);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::SeekConfirmed), 5000);
        QCOMPARE(first(Kind::SeekConfirmed).token, one);
        QCOMPARE(first(Kind::SeekConfirmed).value, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Advanced), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 10000);
        QCOMPARE(m_events.last().token, two);
    }

    void aSeekAfterTheSwitchIsInTheNext()
    {
        // The next track is current while the last of the old one still fades out.
        const auto one = token(19), two = token(20);
        QVERIFY(playIntoTheTransition(one, two));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Advanced), 5000);
        m_engine->seek(500);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::SeekConfirmed), 5000);
        QCOMPARE(first(Kind::SeekConfirmed).token, two);
        QCOMPARE(first(Kind::SeekConfirmed).value, 500);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 10000);
        QCOMPARE(m_events.last().token, two);
    }

    void aPauseHoldsTheTransition()
    {
        const auto one = token(21), two = token(22);
        QVERIFY(playIntoTheTransition(one, two));
        m_engine->pause();
        QTest::qWait(1000); // past the switch, had it played on
        QVERIFY(!has(Kind::Advanced));
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Advanced), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 10000);
        QCOMPARE(m_events.last().token, two);
    }

    void aSkipInTheTransitionRetiresBoth()
    {
        const auto one = token(23), two = token(24), three = token(25);
        QVERIFY(playIntoTheTransition(one, two));
        const auto mark = m_events.size();
        load(QStringLiteral("tone.flac"), QStringLiteral("flac"), three);
        QTRY_VERIFY_WITH_TIMEOUT(
            m_events.last().token == three && m_events.last().kind == Kind::Ready, 5000);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(m_events.last().kind == Kind::Ended, 5000);
        for (auto i = mark; i < m_events.size(); ++i)
            QCOMPARE(m_events[i].token, three);
    }

    void aFadeOutEndsNaturally()
    {
        // Both analysed (their files come at once): the next track comes in from its first
        // sound once the fade-out is 20 dB down (about 5.7 s), and is current from there.
        const auto one = token(26), two = token(27);
        m_server.bodies.insert(QStringLiteral("/a"), fixture(QStringLiteral("fade.mp3")));
        m_server.bodies.insert(QStringLiteral("/b"), fixture(QStringLiteral("long.mp3")));
        m_engine->load(source(m_server.bodies[QStringLiteral("/a")], QStringLiteral("mp3"),
                              QStringLiteral("/a")),
                       one);
        m_engine->preload(source(m_server.bodies[QStringLiteral("/b")], QStringLiteral("mp3"),
                                 QStringLiteral("/b")),
                          two);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Advanced), 15000);
        const qint64 lastOfFirst = lastPosition(one);
        // (A plain crossfade would switch at about 6.8 s.)
        QVERIFY2(lastOfFirst > 5000 && lastOfFirst < 6300,
                 qPrintable(QString::number(lastOfFirst)));
        QTRY_VERIFY_WITH_TIMEOUT(lastPosition(two) >= 0, 2000);
        const auto firstOfSecond =
            std::find_if(m_events.begin(), m_events.end(),
                         [&](const auto& e) { return e.token == two && e.kind == Kind::Position; });
        QVERIFY2(firstOfSecond->value < 600, qPrintable(QString::number(firstOfSecond->value)));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ended), 10000);
        QCOMPARE(m_events.last().token, two);
        m_server.bodies.clear();
    }

    void analysesAreKeptForTheNextTime()
    {
        // Played once (the file comes at once): its analysis is kept beside the file...
        const auto a = source(fixture(QStringLiteral("long.mp3")), QStringLiteral("mp3"));
        load(QStringLiteral("long.mp3"), QStringLiteral("mp3"), token(28));
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(!m_store->analysis(a).isEmpty(), 5000);
        const auto kept = automix::Analysis::deserialize(m_store->analysis(a));
        QVERIFY(kept && kept->complete);
        QVERIFY2(kept->soundEndMs > 3800, qPrintable(QString::number(kept->soundEndMs)));
        m_engine->stop();
        // ...and used the next time instead of analysing it again. In this one its sound
        // ends at 3 s, so the transition does too (the next isn't analysed: a plain 1.3 s
        // crossfade, current from about 2.3 s; at about 3.3 s by the file's own analysis).
        auto made = *kept;
        made.soundEndMs = 3000;
        m_store->keepAnalysis(a, made.serialize());
        m_events.clear();
        const auto one = token(29), two = token(30);
        load(QStringLiteral("long.mp3"), QStringLiteral("mp3"), one);
        auto other = source(m_server.body, QStringLiteral("mp3"));
        other.identity = ContentHash{};
        m_engine->preload(other, two);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Advanced), 10000);
        const qint64 lastOfFirst = lastPosition(one);
        QVERIFY2(lastOfFirst > 1800 && lastOfFirst < 2700,
                 qPrintable(QString::number(lastOfFirst)));
        m_engine->stop();
    }

    void gaplessWaitsForTheEnd()
    {
        m_engine->setTransition(TransitionMode::Gapless, 6000);
        const auto first = token(15), second = token(16);
        load(QStringLiteral("long.mp3"), QStringLiteral("mp3"), first);
        auto next = source(m_server.body, QStringLiteral("mp3"));
        next.identity = ContentHash{};
        m_engine->preload(next, second);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        m_engine->play();
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Advanced), 10000);
        qint64 lastOfFirst = -1;
        for (const auto& e : std::as_const(m_events)) {
            if (e.kind == Kind::Advanced)
                break;
            if (e.kind == Kind::Position)
                lastOfFirst = e.value;
        }
        QVERIFY2(lastOfFirst > 3800, qPrintable(QString::number(lastOfFirst)));
        m_engine->stop();
    }

    void aNewLoadReplacesTheOld()
    {
        load(QStringLiteral("tone.flac"), QStringLiteral("flac"), token(5));
        const auto second = token(6);
        load(QStringLiteral("tone.mp3"), QStringLiteral("mp3"), second);
        QTRY_VERIFY_WITH_TIMEOUT(has(Kind::Ready), 5000);
        QTest::qWait(100);
        for (const auto& e : std::as_const(m_events))
            QCOMPARE(e.token, second);
    }
};
QTEST_GUILESS_MAIN(AudioEngineTest)
#include "audio_engine_test.moc"
