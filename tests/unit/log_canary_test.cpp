#include "../support/fake_http.h"
#include "../support/fake_range_http.h"
#include "application/account/session_coordinator.h"
#include "infrastructure/media/audio_store.h"
#include "infrastructure/netease/api/session_api_client.h"
#include "logging/log.h"
#include "logging/logger.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <future>

// The log never carries a secret (docs/tasks/logging.md §5): credentials, signed
// addresses, bodies and account ids marked CANARY go through the real code paths, in
// detail mode, and none of them may appear in the session's file.
using namespace Qt::StringLiterals;
using namespace cirrus;
using namespace cirrus::domain;
using namespace cirrus::netease;

class LogCanaryTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_dir;

    QString logged()
    {
        log::flush();
        QFile f(log::currentFile());
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

  private slots:
    void init()
    {
        // Under the build tree: the audio cache refuses /tmp (world-writable).
        m_dir = std::make_unique<QTemporaryDir>(QCoreApplication::applicationDirPath() +
                                                QStringLiteral("/log-canary-XXXXXX"));
        QVERIFY(m_dir->isValid());
        log::install(false);
        log::setVerbose(true);
        log::FileOptions options;
        options.directory = m_dir->filePath(QStringLiteral("logs"));
        QVERIFY(log::start(options));
    }
    void cleanup()
    {
        log::shutdown();
        m_dir.reset();
    }

    void apiRequests()
    {
        auto manager = std::make_unique<test::FakeManager>();
        test::Plan key{R"({"code":200,"unikey":"CANARY-QR-KEY"})"};
        key.headers = {{"Set-Cookie", "MUSIC_U=CANARY-MUSIC-U; Domain=.music.163.com; Path=/"},
                       {"Set-Cookie", "__csrf=CANARY-CSRF; Path=/"}};
        test::Plan failed{"CANARY-BODY", 0};
        failed.error = QNetworkReply::ProtocolFailure;
        failed.headers = {{"Retry-After", "3"}};
        test::Plan garbage{"CANARY-NOT-JSON"};
        manager->plans = {key, failed, garbage};
        SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                                QStringLiteral("CANARY-DEVICE"));
        const SessionToken context{QUuid::createUuid(), 1};
        QVERIFY(client.advanceTo(context));
        QSignalSpy spy(&client, &SessionApiClient::completed);
        (void)client.submit(context, ApiOperation::QrKey);
        QTRY_COMPARE(spy.size(), 1);
        (void)client.submit(context, ApiOperation::QrCheck,
                            {{QStringLiteral("key"), QStringLiteral("CANARY-QR-KEY")}});
        QTRY_COMPARE(spy.size(), 2);
        (void)client.submit(context, ApiOperation::QrKey);
        QTRY_COMPARE(spy.size(), 3);
        // Qt's own output too: a URL's query is cut whoever logs it.
        qWarning() << QUrl(QStringLiteral("https://music.163.com/x?MUSIC_U=CANARY-QUERY"));

        const QString text = logged();
        QVERIFY2(!text.contains(u"CANARY"), qPrintable(text));
        QVERIFY(QRegularExpression(
                    u"cirrus.api api.done req=\\d+ path=/api/login/qrcode/unikey code=200"_s)
                    .match(text)
                    .hasMatch());
        QVERIFY(text.contains(u"cirrus.net request.failed"));
        QVERIFY(text.contains(u"qt=ProtocolFailure"));
        QVERIFY(text.contains(u"cirrus.api api.failed"));
    }

    void audioDownloads()
    {
        QByteArray body(300 * 1024, 'a');
        test::RangeServer server{body};
        server.statuses = {500}; // one failed range first
        media::AudioStore::Options options;
        options.root = m_dir->filePath(QStringLiteral("audio"));
        options.retryDelayMs = 10;
        media::AudioStore store(options, std::make_unique<test::RangeManager>(server));
        store.setPartition(QStringLiteral("account-a"));
        MediaSource source{QUrl(QStringLiteral("https://m801.music.126.net/CANARY-SIGN/a.mp3"
                                               "?vuutv=CANARY-QUERY")),
                           "exhigh", "mp3", body.size(), QDeadlineTimer(60000)};
        source.identity = ContentHash{QCryptographicHash::hash(body, QCryptographicHash::Md5).toHex()};
        auto stream = store.open(source);
        QVERIFY(stream);
        auto read = std::async(std::launch::async, [stream] {
            QByteArray out(stream->size(), Qt::Uninitialized);
            qint64 at = 0;
            while (at < out.size()) {
                const auto got = stream->read(at, out.data() + at, out.size() - at);
                if (got <= 0)
                    break;
                at += got;
            }
            return at;
        });
        QTRY_VERIFY_WITH_TIMEOUT(
            read.wait_for(std::chrono::seconds(0)) == std::future_status::ready, 5000);
        QCOMPARE(read.get(), body.size());
        QTRY_VERIFY_WITH_TIMEOUT(logged().contains(u"audio.cached"), 5000);

        const QString text = logged();
        QVERIFY2(!text.contains(u"CANARY"), qPrintable(text));
        QVERIFY(text.contains(u"audio.open dl=1"));
        QVERIFY(text.contains(u"host=m801.music.126.net"));
        QVERIFY(text.contains(u"audio.range_failed dl=1 status=500"));
    }

    void accountsAreHashed()
    {
        application::SessionCoordinator session;
        const auto attempt = session.beginAuthentication();
        const auto prepared = session.prepareAuthentication(attempt, QStringLiteral("8000000123"));
        QVERIFY(prepared);
        QVERIFY(session.completeAuthentication(*prepared));
        const QString text = logged();
        QVERIFY(!text.contains(u"8000000123"));
        QVERIFY(text.contains(u"session.phase from=Committing to=Authenticated"));
        QVERIFY(text.contains(u"account=" + log::account(u"8000000123").value));
    }
};
QTEST_GUILESS_MAIN(LogCanaryTest)
#include "log_canary_test.moc"
