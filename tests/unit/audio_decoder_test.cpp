#include "../support/fake_range_http.h"
#include "../support/audio_decoder_access.h"
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
class EvidenceSource final : public media::ByteSource
{
  public:
    std::shared_ptr<media::LocalFileSource> file = media::LocalFileSource::open(
        QStringLiteral(AUDIO_FIXTURES "/tone.flac"));
    int successes = 0;
    QList<PlaybackError> failures;
    std::optional<PlaybackError> reason;
    bool interruptRead = false;
    qint64 size() const override { return file->size(); }
    qint64 read(qint64 at, char* out, qint64 count) override
    {
        return interruptRead || reason ? -1 : file->read(at, out, count);
    }
    std::optional<PlaybackError> failure() const override { return reason; }
    void decoded() override { ++successes; }
    void decodeFailed(PlaybackError error) override { failures.append(error); }
};
int againCalls = 0;
int brieflyAgain(AVFormatContext* format, AVPacket* packet)
{
    return ++againCalls <= 3 ? AVERROR(EAGAIN) : av_read_frame(format, packet);
}
int alwaysAgain(AVFormatContext*, AVPacket*)
{
    ++againCalls;
    return AVERROR(EAGAIN);
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
        QTest::addColumn<qint64>("encodedFrames");
        QTest::newRow("mp3") << QStringLiteral("mp3") << qint64{21 * 1152};
        QTest::newRow("flac") << QStringLiteral("flac") << ToneFrames;
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
        QFETCH(qint64, encodedFrames);
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
        // FFmpeg 6.1 includes MP3 encoder delay/padding in the stream duration;
        // newer demuxers subtract it. Check the two exact fixture-derived values,
        // independently of the decoded, gapless sample count below.
        const qint64 pcmMs = ToneFrames * 1000 / rate;
        const qint64 encodedMs = (encodedFrames * 1000 + rate / 2) / rate;
        qInfo() << "Stream duration (ms):" << duration << "PCM:" << pcmMs
                << "encoded:" << encodedMs;
        QVERIFY2(
            duration == pcmMs || duration == encodedMs,
            qPrintable(QStringLiteral("Unexpected stream duration: %1 ms; expected %2 or %3 ms")
                           .arg(duration)
                           .arg(pcmMs)
                           .arg(encodedMs)));
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

    void normalEofCertifiesOnlyLinearDecoding()
    {
        auto bytes = std::make_shared<EvidenceSource>();
        QVERIFY(bytes->file);
        AudioDecoder decoder;
        QVERIFY(decoder.open(bytes, QStringLiteral("flac")));
        QCOMPARE(qint64(decodeAll(decoder).size() / 2), ToneFrames);
        QVERIFY(decoder.atEnd());
        QVERIFY(!decoder.error());
        QCOMPARE(bytes->successes, 1);
        QVERIFY(bytes->failures.isEmpty());
        decodeAll(decoder);
        QCOMPARE(bytes->successes, 1);

        auto sought = std::make_shared<EvidenceSource>();
        AudioDecoder partial;
        QVERIFY(partial.open(sought, QStringLiteral("flac")));
        QVERIFY(partial.seek(250));
        QCOMPARE(qint64(decodeAll(partial).size() / 2), ToneFrames - 11025);
        QVERIFY(partial.atEnd());
        QVERIFY(!partial.error());
        QCOMPARE(sought->successes, 0);
    }

    void demuxErrorIsNotEof()
    {
        auto bytes = std::make_shared<EvidenceSource>();
        QVERIFY(bytes->file);
        AudioDecoder decoder;
        QVERIFY(decoder.open(bytes, QStringLiteral("flac")));
        media::AudioDecoderTestAccess::reader(decoder, &media::AudioDecoderTestAccess::invalid);
        decodeAll(decoder);
        QCOMPARE(decoder.error(), std::optional(PlaybackError::Decode));
        QCOMPARE(bytes->failures, QList<PlaybackError>{PlaybackError::Decode});
        QCOMPARE(bytes->successes, 0);
    }

    void demuxAgainIsBounded()
    {
        auto bytes = std::make_shared<EvidenceSource>();
        QVERIFY(bytes->file);
        AudioDecoder decoder;
        QVERIFY(decoder.open(bytes, QStringLiteral("flac")));
        againCalls = 0;
        media::AudioDecoderTestAccess::reader(decoder, &brieflyAgain);
        QCOMPARE(qint64(decodeAll(decoder).size() / 2), ToneFrames);
        QVERIFY(!decoder.error());
        QCOMPARE(bytes->successes, 1);

        AudioDecoder blocked;
        QVERIFY(blocked.open(bytes, QStringLiteral("flac")));
        againCalls = 0;
        media::AudioDecoderTestAccess::reader(blocked, &alwaysAgain);
        decodeAll(blocked);
        QCOMPARE(againCalls, 4);
        QCOMPARE(blocked.error(), std::optional(PlaybackError::Decode));
    }

    void readFailureAndInterruptionKeepTheirMeaning()
    {
        for (bool interrupted : {false, true}) {
            auto bytes = std::make_shared<EvidenceSource>();
            QVERIFY(bytes->file);
            AudioDecoder decoder;
            QVERIFY(decoder.open(bytes, QStringLiteral("flac")));
            // Force a source read after open, independently of FFmpeg's read-ahead.
            media::AudioDecoderTestAccess::reader(decoder, &media::AudioDecoderTestAccess::readSource);
            bytes->interruptRead = interrupted;
            if (!interrupted)
                bytes->reason = PlaybackError::Network;
            decodeAll(decoder);
            QCOMPARE(bytes->successes, 0);
            if (interrupted) {
                QVERIFY(decoder.interrupted());
                QVERIFY(!decoder.error());
                QVERIFY(bytes->failures.isEmpty());
            } else {
                QCOMPARE(decoder.error(), std::optional(PlaybackError::Network));
            }
        }
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
