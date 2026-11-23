#include "infrastructure/media/loudness.h"
#include "infrastructure/media/media_source_policy.h"
#include "infrastructure/netease/music/stream_resolver.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus;
using namespace domain;
namespace
{
QJsonObject row()
{
    return {{"id", "9223372036854775807"},
            {"code", 200},
            {"url", "http://m802.music.126.net/synthetic/test.mp3?token=synthetic"},
            {"level", "standard"},
            {"type", "mp3"},
            {"size", 12345},
            {"expi", 1200},
            {"fee", 1},
            {"payed", 0},
            {"freeTrialInfo", QJsonObject{{"start", 30.5}, {"end", 60.5}}}};
}
application::StreamResult map(QJsonObject value, qint64 elapsed = 0)
{
    return netease::mapStreamResponse({{"9223372036854775807"}, AudioQuality::HiRes},
                                      {{"code", 200}, {"data", QJsonArray{value}}}, elapsed);
}
} // namespace
class StreamTest : public QObject
{
    Q_OBJECT
  private slots:
    void contract()
    {
        const auto c = netease::operationContract(netease::ApiOperation::SongStream);
        QVERIFY(c);
        QCOMPARE(c->protocol, netease::Protocol::Xeapi);
        auto p = netease::operationPayload(netease::ApiOperation::SongStream,
                                           {{"id", "9223372036854775807"}, {"level", "hires"}});
        QVERIFY(p);
        QCOMPARE(p->value("ids").toString(), "[9223372036854775807]");
        QCOMPARE(p->value("encodeType").toString(), "flac");
        QVERIFY(!netease::operationPayload(netease::ApiOperation::SongStream,
                                           {{"id", "1"}, {"level", "vivid"}}));
        QVERIFY(
            !netease::operationPayload(netease::ApiOperation::SongStream,
                                       {{"id", "1"}, {"level", "standard"}, {"unblock", true}}));
        QVERIFY(!netease::operationPayload(netease::ApiOperation::SongStream,
                                           {{"id", 1}, {"level", "standard"}}));
    }
    void loudness()
    {
        auto r = row();
        QVERIFY(!std::get<StreamSource>(map(r)).gainDb); // none given
        r["gain"] = -11.5;
        r["peak"] = 0.98;
        auto source = std::get<StreamSource>(map(r));
        QVERIFY(source.gainDb);
        QCOMPARE(*source.gainDb, -11.5f);
        QCOMPARE(source.peak, 0.98f);
        r["peak"] = 1.05; // capped
        QCOMPARE(std::get<StreamSource>(map(r)).peak, 1.0f);
        r["gain"] = 1.5; // what preview clips carry: a placeholder
        r["peak"] = 0;
        QVERIFY(!std::get<StreamSource>(map(r)).gainDb);
        r["gain"] = "-3";
        r["peak"] = 1;
        QVERIFY(!std::get<StreamSource>(map(r)).gainDb); // odd: dropped, not a failure
        r["gain"] = 400;
        QVERIFY(!std::get<StreamSource>(map(r)).gainDb);

        // To −14 LUFS: gain + 4 dB, no boost past the peak, unchanged when unknown.
        MediaSource m;
        QCOMPARE(media::normalizationGain(m), 1.0f);
        m.gainDb = -10.0f;
        m.peak = 1.0f;
        QVERIFY(qAbs(media::normalizationGain(m) - std::pow(10.0f, -6.0f / 20)) < 1e-5f);
        m.gainDb = 2.0f; // +6 dB wanted, the peak allows +0.9 dB
        m.peak = 0.9f;
        QVERIFY(qAbs(media::normalizationGain(m) - 1.0f / 0.9f) < 1e-5f);
        m.gainDb = 2.0f;
        m.peak = 0.25f; // room for +12 dB: the full +6 dB
        QVERIFY(qAbs(media::normalizationGain(m) - std::pow(10.0f, 6.0f / 20)) < 1e-5f);
    }
    void previewAndActualQuality()
    {
        auto result = map(row());
        QVERIFY(std::holds_alternative<StreamSource>(result));
        const auto source = std::get<StreamSource>(result);
        QCOMPARE(source.actualQuality, "standard"); // request hires, actual remains standard
        QCOMPARE(source.url.scheme(), "https");
        QVERIFY(source.preview);
        QCOMPARE(source.preview->startMs, 30500);
        QCOMPARE(source.preview->endMs, 60500);
        QVERIFY(source.validUntil.remainingTime() <= 1200000);
        QVERIFY(contentMd5(source).isEmpty()); // none given: nothing to key a cache on
        auto r = row();
        r["md5"] = "8BE6391A4A129BE0AD9FEF58192DF3CC";
        QCOMPARE(contentMd5(std::get<StreamSource>(map(r))),
                 QByteArray("8be6391a4a129be0ad9fef58192df3cc"));
        r["md5"] = "not-a-digest"; // dropped, not a failure
        QVERIFY(contentMd5(std::get<StreamSource>(map(r))).isEmpty());
        r = row();
        r["freeTrialInfo"] = QJsonValue::Null;
        result = map(r);
        QVERIFY(std::holds_alternative<StreamSource>(result));
        QVERIFY(!std::get<StreamSource>(result).preview); // no inferred full-track grant
        r["url"] = QJsonValue::Null;
        QCOMPARE(std::get<PlaybackFailure>(map(r)).error, PlaybackError::NeedPermission);
    }
    void malformed_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("wrong-id") << QString("id") << QJsonValue("1");
        QTest::newRow("wide-number") << QString("id") << QJsonValue(9007199254740992.0);
        QTest::newRow("fractional-ttl") << QString("expi") << QJsonValue(1.5);
        QTest::newRow("missing-ttl") << QString("expi") << QJsonValue(QJsonValue::Undefined);
        QTest::newRow("null-size") << QString("size") << QJsonValue(QJsonValue::Null);
        QTest::newRow("zero-size") << QString("size") << QJsonValue(0);
        QTest::newRow("string-preview") << QString("freeTrialInfo") << QJsonValue("null");
        QTest::newRow("invalid-preview")
            << QString("freeTrialInfo") << QJsonValue(QJsonObject{{"start", 20}, {"end", 10}});
        QTest::newRow("odd-quality") << QString("level") << QJsonValue("Hi-Res");
    }
    void malformed()
    {
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);
        auto r = row();
        r[field] = value;
        QCOMPARE(std::get<PlaybackFailure>(map(r)).error, PlaybackError::InvalidResponse);
    }
    void aProgramsAudioHasNoLevel()
    {
        auto r = row();
        r.remove("level"); // a podcast program's track, as the service sends it
        const auto source = std::get<StreamSource>(map(r));
        QVERIFY(source.actualQuality.isEmpty());
    }
    void expiredAndRejected()
    {
        QCOMPARE(std::get<PlaybackFailure>(map(row(), 1200000)).error, PlaybackError::Expired);
        auto r = row();
        r["code"] = 404;
        const auto f = std::get<PlaybackFailure>(map(r));
        QCOMPARE(f.error, PlaybackError::NeedPermission);
        QCOMPARE(f.providerCode, 404);
        r = row();
        r["url"] = "https://example.com/test.mp3";
        QCOMPARE(std::get<PlaybackFailure>(map(r)).error, PlaybackError::UnsafeSource);
        r = row();
        r["type"] = "m3u8";
        QCOMPARE(std::get<PlaybackFailure>(map(r)).error, PlaybackError::UnsupportedFormat);
        QVERIFY(std::holds_alternative<PlaybackFailure>(netease::mapStreamResponse(
            {{"1"}}, {{"code", 200}, {"data", QJsonArray{row(), row()}}})));
    }
    // An empty address is NeedPermission only on the provider's own evidence (fee > 0 or
    // cannotListenReason 1); otherwise it stays Unavailable (a network/expiry symptom).
    void emptyAddressIsClassifiedByEvidence()
    {
        auto r = row(); // fee 1, freeTrialInfo present
        r["url"] = QJsonValue::Null;
        QCOMPARE(std::get<PlaybackFailure>(map(r)).error, PlaybackError::NeedPermission); // fee 1
        r["fee"] = 0;
        QCOMPARE(std::get<PlaybackFailure>(map(r)).error, PlaybackError::Unavailable);
        r["freeTrialPrivilege"] = QJsonObject{{"cannotListenReason", 1}};
        QCOMPARE(std::get<PlaybackFailure>(map(r)).error, PlaybackError::NeedPermission);
        r["freeTrialPrivilege"] = QJsonObject{{"cannotListenReason", 2}};
        QCOMPARE(std::get<PlaybackFailure>(map(r)).error, PlaybackError::Unavailable);
        r["freeTrialPrivilege"] = QJsonValue::Null;
        r["code"] = 404; // the row's own code too
        QCOMPARE(std::get<PlaybackFailure>(map(r)).error, PlaybackError::NeedPermission);
    }
    void errorEnvelopeMapping()
    {
        netease::SessionApiClient client("synthetic-device");
        const SessionToken token{QUuid::createUuid(), 1};
        QVERIFY(client.advanceTo(token));
        netease::StreamResolver resolver(client);
        QSignalSpy spy(&resolver, &application::IStreamResolver::completed);
        struct Case {
            int status;
            netease::TransportError transport;
            PlaybackError expected;
        };
        const Case cases[] = {
            {0, netease::TransportError::Timeout, PlaybackError::Timeout},
            {429, netease::TransportError::HttpStatus, PlaybackError::RateLimited},
            {403, netease::TransportError::HttpStatus, PlaybackError::Unavailable}};
        for (const auto& item : cases) {
            const auto id = resolver.resolve(token, {{"1"}});
            netease::ApiResult result;
            result.error = netease::ApiError::Transport;
            result.transportError = item.transport;
            result.httpStatus = item.status;
            // Synthetic original-token completion. No installed key: queued client work
            // cannot dispatch real IO, and its later missing-key event is ignored.
            emit client.completed(id, token, result);
            QCOMPARE(spy.size(), 1);
            QCOMPARE(std::get<PlaybackFailure>(
                         qvariant_cast<application::StreamResult>(spy.takeFirst()[2]))
                         .error,
                     item.expected);
        }
    }
    void missingKeyIsNotDowngraded()
    {
        netease::SessionApiClient client("synthetic-device");
        const SessionToken token{QUuid::createUuid(), 1};
        QVERIFY(client.advanceTo(token));
        netease::StreamResolver resolver(client);
        QSignalSpy spy(&resolver, &application::IStreamResolver::completed);
        QVERIFY(resolver.resolve(token, {{"1"}}));
        QTRY_COMPARE(spy.size(), 1);
        const auto result = qvariant_cast<application::StreamResult>(spy.first()[2]);
        QCOMPARE(std::get<PlaybackFailure>(result).error, PlaybackError::ProtocolKeyMissing);
        QCOMPARE(qvariant_cast<SessionToken>(spy.first()[1]), token);
    }
};
QTEST_GUILESS_MAIN(StreamTest)
#include "stream_test.moc"
