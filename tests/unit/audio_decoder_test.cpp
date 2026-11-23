#include "../support/fake_range_http.h"
#include "infrastructure/media/audio_decoder.h"
#include "infrastructure/media/audio_store.h"
#include "infrastructure/media/byte_source.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <future>
using namespace cirrus;
using namespace domain;
using media::AudioDecoder;
using media::AudioStore;
namespace
{
constexpr qint64 ToneFrames = 22050; // tests/fixtures/audio/README.md

QByteArray fixture(const QString& name)
{
    QFile file(QStringLiteral(AUDIO_FIXTURES "/") + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
MediaSource source(const QByteArray& body, const QString& format)
{
    MediaSource s{QUrl(QStringLiteral("https://m801.music.126.net/tone")), "standard", format,
                  body.size(), QDeadlineTimer(60000)};
    s.identity = ContentHash{QCryptographicHash::hash(body, QCryptographicHash::Md5).toHex()};
    return s;
}
// Decoding blocks on the download: run it on a thread while the test spins the loop.
template <class F> auto onThread(F work)
{
    auto future = std::async(std::launch::async, std::move(work));
    while (future.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready)
        QCoreApplication::processEvents(); // the store downloads on this thread
    return future.get();
}
std::vector<float> decodeAll(AudioDecoder& decoder)
{
    std::vector<float> all;
    while (decoder.read(all, 4096)) {
    }
    return all;
}
} // namespace

class AudioDecoderTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_dir;

    std::unique_ptr<AudioStore> store(test::RangeServer& server)
    {
        AudioStore::Options options;
        options.root = m_dir->path() + QStringLiteral("/audio");
        options.retryDelayMs = 10;
        auto s =
            std::make_unique<AudioStore>(options, std::make_unique<test::RangeManager>(server));
        s->setPartition(QStringLiteral("test"));
        return s;
    }

  private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>(QCoreApplication::applicationDirPath() +
                                                QStringLiteral("/audio-decoder-XXXXXX"));
        QVERIFY(m_dir->isValid());
    }
    void cleanup() { m_dir.reset(); }

    void decodesTheWholeFile_data()
    {
        QTest::addColumn<QString>("format");
        QTest::newRow("mp3") << QStringLiteral("mp3");
        QTest::newRow("flac") << QStringLiteral("flac");
    }

    // MP4/fMP4 audio: AAC (moov after mdat and fragmented) and E-AC-3, the Bilibili CDN's
    // audio/mp4 formats. Same 0.5 s / 44.1 kHz / 22050 frames as the tones.
    void decodesMp4Audio_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("format");
        QTest::newRow("aac") << QStringLiteral("tone_aac.m4a") << QStringLiteral("aac");
        QTest::newRow("aac-fmp4")
            << QStringLiteral("tone_fmp4.m4a") << QStringLiteral("aac");
        QTest::newRow("eac3") << QStringLiteral("tone_eac3.m4a") << QStringLiteral("eac3");
        QTest::newRow("flac-mp4") << QStringLiteral("tone_flac.m4a") << QStringLiteral("flac-mp4");
    }
    void decodesMp4Audio()
    {
        QFETCH(QString, file);
        QFETCH(QString, format);
        test::RangeServer server{fixture(file)};
        QVERIFY(!server.body.isEmpty());
        auto s = store(server);
        auto stream = s->open(source(server.body, format));
        QVERIFY(stream);
        const auto [opened, rate, frames] = onThread([stream, format] {
            AudioDecoder decoder;
            const bool ok = decoder.open(stream, format);
            const auto all = ok ? decodeAll(decoder) : std::vector<float>();
            return std::tuple{ok, decoder.sampleRate(), qint64(all.size() / 2)};
        });
        QVERIFY(opened);
        QCOMPARE(rate, 44100);
        // Lossy codecs pad: the count is near the source, not frame-exact.
        QVERIFY(std::abs(frames - ToneFrames) <= 2048);
    }
    void decodesTheWholeFile()
    {
        QFETCH(QString, format);
        test::RangeServer server{fixture(QStringLiteral("tone.") + format)};
        QVERIFY(!server.body.isEmpty());
        auto s = store(server);
        auto stream = s->open(source(server.body, format));
        QVERIFY(stream);
        const auto [opened, rate, duration, frames] = onThread([stream, format] {
            AudioDecoder decoder;
            const bool ok = decoder.open(stream, format);
            const auto all = ok ? decodeAll(decoder) : std::vector<float>();
            return std::tuple{ok, decoder.sampleRate(), decoder.durationMs(),
                              qint64(all.size() / 2)};
        });
        QVERIFY(opened);
        QCOMPARE(rate, 44100);
        QVERIFY(std::abs(duration - 500) <= 30);
        // Exact: the MP3 encoder's delay and padding are trimmed (gapless).
        QCOMPARE(frames, ToneFrames);
    }

    void decodesALocalFile_data() { decodesTheWholeFile_data(); }
    void decodesALocalFile()
    {
        QFETCH(QString, format);
        // The automix tool reads files straight from disk, past the store.
        auto file = media::LocalFileSource::open(QStringLiteral(AUDIO_FIXTURES "/tone.") + format);
        QVERIFY(file);
        AudioDecoder decoder;
        QVERIFY(decoder.open(file, format));
        QCOMPARE(qint64(decodeAll(decoder).size() / 2), ToneFrames);
        QVERIFY(decoder.seek(250));
        std::vector<float> rest = decodeAll(decoder);
        QCOMPARE(qint64(rest.size() / 2), ToneFrames - 11025);
        QVERIFY(!media::LocalFileSource::open(QStringLiteral(AUDIO_FIXTURES "/missing.flac")));
    }

    void seekIsSampleExact_data() { decodesTheWholeFile_data(); }
    void seekIsSampleExact()
    {
        QFETCH(QString, format);
        test::RangeServer server{fixture(QStringLiteral("tone.") + format)};
        auto s = store(server);
        auto stream = s->open(source(server.body, format));
        const auto [linear, sought, position] = onThread([stream, format] {
            AudioDecoder decoder;
            decoder.open(stream, format);
            const auto all = decodeAll(decoder);
            decoder.seek(250);
            const qint64 at = decoder.position();
            std::vector<float> after;
            while (after.size() < 4000 && decoder.read(after, 4000 - after.size() / 2)) {
            }
            return std::tuple{all, after, at};
        });
        QCOMPARE(position, 11025);
        QVERIFY(sought.size() >= 4000);
        for (size_t i = 0; i < 4000; ++i)
            QCOMPARE(sought[i], linear[11025 * 2 + i]);
    }

    void declaredFormatMustMatch()
    {
        test::RangeServer server{fixture(QStringLiteral("tone.flac"))};
        auto s = store(server);
        for (const auto& declared : {QStringLiteral("mp3"), QStringLiteral("ogg")}) {
            auto stream = s->open(source(server.body, declared));
            const auto error = onThread([stream, declared] {
                AudioDecoder decoder;
                return decoder.open(stream, declared) ? std::nullopt : decoder.error();
            });
            QCOMPARE(error, std::optional(PlaybackError::UnsupportedFormat));
            stream.reset();
        }
        test::RangeServer garbage{QByteArray(64 * 1024, '\x5a')};
        auto t = store(garbage);
        auto stream = t->open(source(garbage.body, QStringLiteral("mp3")));
        const auto error = onThread([stream] {
            AudioDecoder decoder;
            return decoder.open(stream, QStringLiteral("mp3")) ? std::nullopt : decoder.error();
        });
        QVERIFY(error == PlaybackError::UnsupportedFormat || error == PlaybackError::Decode);
    }

    void aFailedDownloadIsTheReason()
    {
        test::RangeServer server{fixture(QStringLiteral("tone.mp3"))};
        server.statuses = {500, 500, 500, 500};
        auto s = store(server);
        auto stream = s->open(source(server.body, QStringLiteral("mp3")));
        const auto error = onThread([stream] {
            AudioDecoder decoder;
            return decoder.open(stream, QStringLiteral("mp3")) ? std::nullopt : decoder.error();
        });
        QCOMPARE(error, std::optional(PlaybackError::Network));
    }
};
QTEST_GUILESS_MAIN(AudioDecoderTest)
#include "audio_decoder_test.moc"
