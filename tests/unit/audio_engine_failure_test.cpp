#include "../support/audio_decoder_access.h"
#include "../support/fake_range_http.h"
#include "infrastructure/media/audio_engine_producer_p.h"
#include "infrastructure/media/byte_source.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace cirrus::media
{
using namespace domain;
using application::PlaybackEvent;
using Kind = PlaybackEvent::Kind;

class AudioEngineFailureTest : public QObject
{
    Q_OBJECT
    static std::unique_ptr<AudioDecoder> decoder()
    {
        auto bytes = LocalFileSource::open(QStringLiteral(AUDIO_FIXTURES "/tone.flac"));
        auto result = std::make_unique<AudioDecoder>();
        if (!bytes || !result->open(bytes, QStringLiteral("flac")))
            return {};
        return result;
    }
    static MediaSource source(qint64 size)
    {
        return {QUrl(QStringLiteral("https://m801.music.126.net/tone.flac")), "standard",
                "flac", size, QDeadlineTimer(60000)};
    }
    static void flushCallbacks()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents();
    }
    static void seed(AudioEngine& engine, const MediaSource& source)
    {
        engine.m_active = engine.m_ready = true;
        engine.m_clock.start();
        engine.m_token = LoadToken{{}, 1};
        engine.m_source = source;
        engine.m_stream = engine.m_store.open(source);
        engine.m_next = AudioEngine::Next{source, LoadToken{{}, 2}, engine.m_store.open(source)};
        auto producer = std::make_shared<AudioEngine::Producer>();
        producer->outRate = 44100;
        producer->nextToken = LoadToken{{}, 2};
        producer->nextStream = engine.m_next->stream;
        producer->nextFormat = QStringLiteral("flac");
        producer->m_curToken = LoadToken{{}, 1};
        producer->m_cur = std::make_unique<automix::Deck>(decoder(), 44100, 1, false);
        engine.m_producer = std::move(producer);
    }
    static void mixing(AudioEngine::Producer& producer)
    {
        automix::Plan plan;
        plan.style = automix::Style::Crossfade;
        plan.overlapMs = 100;
        plan.switchMs = 50;
        producer.m_upcoming = AudioEngine::Producer::Upcoming{
            producer.nextStream, decoder(), LoadToken{{}, 2}, 1, false, plan};
    }

  private slots:
    // All routes use real decoders/decks and owner callbacks without an audio device.
    void incomingFailure_data()
    {
        QTest::addColumn<int>("route");
        QTest::addColumn<bool>("notified");
        for (int route = 0; route < 3; ++route) {
            QTest::newRow(qPrintable(QStringLiteral("route-%1-before-advance").arg(route)))
                << route << false;
            QTest::newRow(qPrintable(QStringLiteral("route-%1-after-advance").arg(route)))
                << route << true;
        }
    }
    void incomingFailure()
    {
        QFETCH(int, route);
        QFETCH(bool, notified);
        QFile file(QStringLiteral(AUDIO_FIXTURES "/tone.flac"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        test::RangeServer server{file.readAll()};
        AudioStore::Options options;
        AudioStore store(options, std::make_unique<test::RangeManager>(server));
        AudioEngine engine(store);
        seed(engine, source(server.body.size()));
        auto producer = engine.m_producer;
        QTRY_VERIFY(engine.m_next->stream->complete());
        QList<PlaybackEvent> events;
        connect(&engine, &AudioEngine::event, this, [&](auto value) { events.append(value); });
        if (route == 0) {
            std::vector<float> audio(44100 * 2);
            producer->m_cur->pull(audio.data(), 44100, false);
            QVERIFY(producer->m_cur->decoderEnded());
            QVERIFY(producer->crossNext());
        } else if (route == 1) {
            QVERIFY(producer->crossLate());
        } else {
            mixing(*producer);
            QVERIFY(producer->startMixing());
            std::vector<float> out;
            while (producer->m_mixing)
                QVERIFY(producer->mixSome(&engine, out));
        }
        QCOMPARE(producer->m_curToken, (LoadToken{{}, 2}));
        if (notified)
            engine.advance(producer->nextDurationMs, producer->nextStartFrame,
                           producer->nextBaseMs);
        AudioDecoderTestAccess::reader(producer->m_cur->decoder(), &AudioDecoderTestAccess::invalid);
        std::vector<float> audio(44100 * 2);
        producer->m_cur->pull(audio.data(), 44100, false);
        QCOMPARE(producer->m_cur->decoder().error(), std::optional(PlaybackError::Decode));
        producer->failed(&engine, producer->m_curToken, *producer->m_cur->decoder().error());
        flushCallbacks();
        QTRY_VERIFY(!events.isEmpty() && events.last().kind == Kind::Failure);
        QCOMPARE(events.first().kind, Kind::Advanced);
        QCOMPARE(events.first().token, (LoadToken{{}, 2}));
        QCOMPARE(events.last().token, (LoadToken{{}, 2}));
        QCOMPARE(events.last().failure.error, PlaybackError::Decode);
    }

    void overlapFailure_data()
    {
        QTest::addColumn<bool>("incoming");
        QTest::addColumn<bool>("notified");
        QTest::addColumn<bool>("queuedAdvance");
        QTest::newRow("incoming-before") << true << false << false;
        QTest::newRow("incoming-after") << true << true << false;
        QTest::newRow("incoming-queued-then-advance") << true << false << true;
        QTest::newRow("outgoing-before") << false << false << false;
        QTest::newRow("outgoing-after") << false << true << false;
        QTest::newRow("outgoing-queued-then-advance") << false << false << true;
    }
    void overlapFailure()
    {
        QFETCH(bool, incoming);
        QFETCH(bool, notified);
        QFETCH(bool, queuedAdvance);
        QFile file(QStringLiteral(AUDIO_FIXTURES "/tone.flac"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        test::RangeServer server{file.readAll()};
        AudioStore store({}, std::make_unique<test::RangeManager>(server));
        AudioEngine engine(store);
        seed(engine, source(server.body.size()));
        auto producer = engine.m_producer;
        mixing(*producer);
        QVERIFY(producer->startMixing());
        QList<PlaybackEvent> events;
        connect(&engine, &AudioEngine::event, this, [&](auto value) { events.append(value); });
        if (notified)
            engine.advance(producer->nextDurationMs, producer->nextStartFrame,
                           producer->nextBaseMs);
        auto& damaged = incoming ? producer->m_mixing->deck->decoder() : producer->m_cur->decoder();
        AudioDecoderTestAccess::reader(damaged, &AudioDecoderTestAccess::invalid);
        std::vector<float> out;
        QCOMPARE(producer->mixSome(&engine, out), !incoming);
        if (queuedAdvance)
            engine.advance(producer->nextDurationMs, producer->nextStartFrame,
                           producer->nextBaseMs);
        flushCallbacks();
        QTRY_VERIFY(!events.isEmpty() && events.last().kind == Kind::Failure);
        QCOMPARE(events.last().token, (LoadToken{{}, incoming ? 2U : 1U}));
        QCOMPARE(engine.m_active, !incoming && (notified || queuedAdvance));
        if (!incoming && (notified || queuedAdvance)) {
            QCOMPARE(engine.m_token, (LoadToken{{}, 2}));
            while (producer->m_mixing)
                QVERIFY(producer->mixSome(&engine, out));
            QCOMPARE(producer->m_curToken, (LoadToken{{}, 2}));
            QVERIFY(!producer->m_cur->decoder().error());
        }
    }

    void lateCallbackCannotFailReplacement()
    {
        AudioStore store({});
        AudioEngine engine(store);
        engine.m_active = engine.m_ready = true;
        engine.m_token = LoadToken{{}, 2};
        auto old = std::make_shared<AudioEngine::Producer>();
        engine.m_producer = old;
        QList<PlaybackEvent> events;
        connect(&engine, &AudioEngine::event, this, [&](auto value) { events.append(value); });
        old->failed(&engine, LoadToken{{}, 2}, PlaybackError::Decode);
        engine.m_producer = std::make_shared<AudioEngine::Producer>();
        flushCallbacks();
        QVERIFY(engine.m_active);
        QVERIFY(events.isEmpty());

        // Also replace between the error callback and fail()'s deferred stop, keeping
        // the token (as a device restart or seek does).
        engine.producerFailed(engine.m_producer, LoadToken{{}, 2}, PlaybackError::Decode);
        engine.m_producer = std::make_shared<AudioEngine::Producer>();
        flushCallbacks();
        QVERIFY(engine.m_active);
        QVERIFY(events.isEmpty());
    }
};
} // namespace cirrus::media
QTEST_GUILESS_MAIN(cirrus::media::AudioEngineFailureTest)
#include "audio_engine_failure_test.moc"
