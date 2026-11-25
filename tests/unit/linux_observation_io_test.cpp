#include "../support/synthetic_playback_observation.h"
#include "infrastructure/ipc/linux_observation_io.h"
#include "infrastructure/ipc/playback_observation_codec.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
using namespace cirrus;
using namespace infrastructure::ipc;
namespace
{
struct Peer {
    int fd = -1;
    QByteArray bytes;
    ~Peer()
    {
        if (fd >= 0)
            ::close(fd);
    }
    bool connectTo(const QString& path)
    {
        fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        auto b = QFile::encodeName(path);
        if (b.size() >= qsizetype(sizeof(a.sun_path)))
            return false;
        std::memcpy(a.sun_path, b.constData(), size_t(b.size()) + 1);
        return ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
               linuxSameUserPeer(fd);
    }
    bool send(QByteArray b)
    {
        return ::send(fd, b.constData(), size_t(b.size()), MSG_DONTWAIT | MSG_NOSIGNAL) == b.size();
    }
    QByteArray next()
    {
        std::array<char, 4096> b{};
        for (int i = 0; i < 8; ++i) {
            auto n = ::recv(fd, b.data(), b.size(), MSG_DONTWAIT);
            if (n <= 0)
                break;
            bytes.append(b.data(), n);
        }
        if (bytes.size() < 4)
            return {};
        quint32 n = 0;
        for (int i = 0; i < 4; ++i)
            n = (n << 8) | uchar(bytes[i]);
        if (n > PlaybackObservationCodec::FrameBytes)
            return {};
        if (bytes.size() < 4 + n)
            return {};
        return bytes.mid(4, n);
    }
    void consume()
    {
        if (bytes.size() < 4)
            return;
        quint32 n = 0;
        for (int i = 0; i < 4; ++i)
            n = (n << 8) | uchar(bytes[i]);
        if (bytes.size() >= 4 + n)
            bytes.remove(0, 4 + n);
    }
    bool eof()
    {
        char c;
        return ::recv(fd, &c, 1, MSG_DONTWAIT | MSG_PEEK) == 0;
    }
};
QByteArray request(QString id, QString method, QJsonObject fields = {})
{
    fields["v"] = 1;
    fields["kind"] = "request";
    fields["id"] = id;
    fields["method"] = method;
    return PlaybackObservationCodec::frame(QJsonDocument(fields).toJson(QJsonDocument::Compact));
}
struct Fixture {
    QTemporaryDir root;
    std::shared_ptr<ObservationMailbox> source = std::make_shared<ObservationMailbox>();
    std::shared_ptr<ObservationWakeTicket> wake = std::make_shared<ObservationWakeTicket>();
    domain::PlaybackObservation value = test::observationValue();
    domain::PlaybackLyricDocument doc = test::observationDocument();
    LinuxObservationIo io{source, wake};
    explicit Fixture(bool published = true)
    {
        value.sequence = 1;
        if (published) {
            source->beginView(value.binding);
            source->publish(value, std::make_shared<const domain::PlaybackLyricDocument>(doc));
        }
    }
    LinuxEndpointError start()
    {
        return io.start({true, root.path(), "ob-io-test", value.binding.instanceId});
    }
    QString socket() const { return QFileInfo(io.manifestPath()).path() + "/s"; }
};
} // namespace
class LinuxObservationIoTest final : public QObject
{
    Q_OBJECT
  private slots:
    void defaultOffAndOneShot()
    {
        Fixture f;
        QVERIFY(f.root.isValid());
        QCOMPARE(f.io.start({}), LinuxEndpointError::Disabled);
        QVERIFY(QDir(f.root.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
        QCOMPARE(f.start(), LinuxEndpointError::None);
        QCOMPARE(f.start(), LinuxEndpointError::InvalidConfig);
        const auto socket = f.socket();
        f.io.stop();
        QVERIFY(!QFileInfo::exists(socket));
    }
    void connectionBeforeFirstViewSurvivesPublication()
    {
        Fixture f(false);
        f.source->retire({});
        QCOMPARE(f.start(), LinuxEndpointError::None);
        Peer p;
        QVERIFY(p.connectTo(f.socket()));
        QVERIFY(p.send(request("h", "hello")));
        // Drive the I/O turn while the owner has deliberately published no view.
        QTRY_VERIFY(f.io.statistics().turns > 0);
        char pending;
        const auto peek = ::recv(p.fd, &pending, 1, MSG_DONTWAIT | MSG_PEEK);
        const auto pendingError = errno;
        QCOMPARE(peek, ssize_t(-1));
        QVERIFY(pendingError == EAGAIN || pendingError == EWOULDBLOCK);
        QCOMPARE(f.io.statistics().currentClients, 0);
        QVERIFY(f.source->beginView(f.value.binding));
        QVERIFY(f.source->publish(f.value,
                                  std::make_shared<const domain::PlaybackLyricDocument>(f.doc)));
        QVERIFY(f.wake->request());
        f.io.wake();
        QByteArray body;
        QTRY_VERIFY(!(body = p.next()).isEmpty());
        const auto hello = QJsonDocument::fromJson(body).object();
        QVERIFY(hello["ok"].toBool());
        QCOMPARE(hello["id"].toString(), QString("h"));
        QCOMPARE(hello["instanceId"].toString(),
                 f.value.binding.instanceId.toString(QUuid::WithoutBraces));
        p.consume();
        QVERIFY(p.send(request("s", "getSnapshot")));
        QTRY_VERIFY(!(body = p.next()).isEmpty());
        const auto response = QJsonDocument::fromJson(body).object();
        QVERIFY(response["ok"].toBool());
        QCOMPARE(response["snapshot"].toObject()["instanceId"].toString(),
                 f.value.binding.instanceId.toString(QUuid::WithoutBraces));
    }
    void helloSnapshotSliceAndBaseline()
    {
        Fixture f;
        QCOMPARE(f.start(), LinuxEndpointError::None);
        Peer p;
        QVERIFY(p.connectTo(f.socket()));
        QVERIFY(p.send(request("h", "hello")));
        QByteArray b;
        QTRY_VERIFY(!(b = p.next()).isEmpty());
        auto o = QJsonDocument::fromJson(b).object();
        QCOMPARE(o["instanceId"].toString(),
                 f.value.binding.instanceId.toString(QUuid::WithoutBraces));
        p.consume();
        QVERIFY(p.send(
            request("s", "subscribe", {{"topics", QJsonArray{"snapshot", "lyrics", "cursor"}}})));
        QTRY_VERIFY(!(b = p.next()).isEmpty());
        o = QJsonDocument::fromJson(b).object();
        QCOMPARE(o["id"].toString(), QString("s"));
        QCOMPARE(o["snapshot"].toObject()["sequence"].toString(), QString("1"));
        p.consume();
        ++f.value.sequence;
        f.value.positionMs = 1300;
        f.value.songPositionMs = 11300;
        QVERIFY(f.source->publish(f.value,
                                  std::make_shared<const domain::PlaybackLyricDocument>(f.doc)));
        QVERIFY(f.wake->request());
        f.io.wake();
        QTRY_VERIFY(!(b = p.next()).isEmpty());
        QCOMPARE(QJsonDocument::fromJson(b).object()["kind"].toString(), QString("event"));
        p.consume();
        auto binding = f.value.binding;
        QJsonObject fields{{"instanceId", binding.instanceId.toString(QUuid::WithoutBraces)},
                           {"viewEpoch", binding.viewEpoch.toString(QUuid::WithoutBraces)},
                           {"trackId", binding.trackId.toString(QUuid::WithoutBraces)},
                           {"lyricsRevision", "2"},
                           {"track", "original.standard"},
                           {"fromLine", 0},
                           {"maxLines", 1}};
        QVERIFY(p.send(request("l", "getLyricsSlice", fields)));
        QTRY_VERIFY(!(b = p.next()).isEmpty());
        o = QJsonDocument::fromJson(b).object();
        QVERIFY(o["ok"].toBool());
        QCOMPARE(o["lines"].toArray()[0].toObject()["text"].toString(),
                 f.doc.lyrics.original.standard.lines[0].text);
        const auto stats = f.io.statistics();
        QVERIFY(stats.maximumInput <= 16384);
        QVERIFY(stats.maximumOutput <= 2 * 1024 * 1024);
        QVERIFY(stats.admittedBytes > 0);
        QVERIFY(stats.maximumAcceptsPerTurn <= 1);
    }
    void fragmentedAndCoalescedRequests()
    {
        Fixture f;
        QCOMPARE(f.start(), LinuxEndpointError::None);
        Peer p;
        QVERIFY(p.connectTo(f.socket()));
        auto h = request("h", "hello");
        QVERIFY(p.send(h.left(2)));
        QTest::qWait(40);
        QVERIFY(p.next().isEmpty());
        QVERIFY(p.send(h.mid(2) + request("s", "getSnapshot")));
        QByteArray a, b;
        QTRY_VERIFY(!(a = p.next()).isEmpty());
        p.consume();
        QTRY_VERIFY(!(b = p.next()).isEmpty());
        QCOMPARE(QJsonDocument::fromJson(a).object()["id"].toString(), QString("h"));
        QCOMPARE(QJsonDocument::fromJson(b).object()["id"].toString(), QString("s"));
    }
    void privacyClosesAlreadyAdmittedClient()
    {
        Fixture f;
        QCOMPARE(f.start(), LinuxEndpointError::None);
        Peer p;
        QVERIFY(p.connectTo(f.socket()));
        QVERIFY(p.send(request("h", "hello")));
        QByteArray b;
        QTRY_VERIFY(!(b = p.next()).isEmpty());
        p.consume();
        f.source->retire(f.value.binding);
        f.io.wake();
        QTRY_VERIFY(p.eof());
        QVERIFY(f.io.running());
    }
    void permissionReplacementStopsListener()
    {
        Fixture f;
        QCOMPARE(f.start(), LinuxEndpointError::None);
        const auto manifest = f.io.manifestPath();
        QVERIFY(QFile::setPermissions(manifest,
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
        QTRY_VERIFY(!f.io.running());
        QVERIFY(!QFileInfo::exists(manifest));
    }
    void deadlines_data()
    {
        QTest::addColumn<bool>("partial");
        QTest::newRow("hello-five-seconds") << false;
        QTest::newRow("partial-two-seconds") << true;
    }
    void deadlines()
    {
        QFETCH(bool, partial);
        Fixture f;
        QCOMPARE(f.start(), LinuxEndpointError::None);
        Peer p;
        QVERIFY(p.connectTo(f.socket()));
        if (partial)
            QVERIFY(p.send(QByteArray(1, '\0')));
        QTRY_VERIFY_WITH_TIMEOUT(p.eof(), partial ? 2800 : 5800);
    }
    void ninthClientRejected()
    {
        Fixture f;
        QCOMPARE(f.start(), LinuxEndpointError::None);
        std::array<std::unique_ptr<Peer>, 9> peers;
        for (auto& p : peers) {
            p = std::make_unique<Peer>();
            QVERIFY(p->connectTo(f.socket()));
            QVERIFY(p->send(request("h", "hello")));
            QTest::qWait(35);
        }
        QByteArray b;
        for (int i = 0; i < 8; ++i)
            QTRY_VERIFY(!(b = peers[i]->next()).isEmpty());
        QTRY_VERIFY(peers[8]->eof());
        QCOMPARE(f.io.statistics().maximumClients, 8);
    }
    void slowRealSocketRetainedBudget()
    {
        Fixture f;
        auto& lines = f.doc.lyrics.original.standard.lines;
        lines.clear();
        for (int i = 0; i < 128; ++i)
            lines.append({i, i + 1, QString(1500, QChar(0x96f2)), {}});
        f.doc.revision = 3;
        f.value.lyricsRevision = 3;
        f.value.cursors = domain::LyricCursors{};
        test::observationSummaries(f.value, f.doc.lyrics);
        ++f.value.sequence;
        QVERIFY(f.source->publish(f.value,
                                  std::make_shared<const domain::PlaybackLyricDocument>(f.doc)));
        QCOMPARE(f.start(), LinuxEndpointError::None);
        Peer p;
        QVERIFY(p.connectTo(f.socket()));
        QVERIFY(p.send(request("h", "hello")));
        QByteArray b;
        QTRY_VERIFY(!(b = p.next()).isEmpty());
        auto binding = f.value.binding;
        QJsonObject fields{{"instanceId", binding.instanceId.toString(QUuid::WithoutBraces)},
                           {"viewEpoch", binding.viewEpoch.toString(QUuid::WithoutBraces)},
                           {"trackId", binding.trackId.toString(QUuid::WithoutBraces)},
                           {"lyricsRevision", "3"},
                           {"track", "original.standard"},
                           {"fromLine", 0},
                           {"maxLines", 128}};
        QByteArray batch;
        for (int i = 0; i < 4; ++i)
            batch += request(QString("r%1").arg(i), "getLyricsSlice", fields);
        QVERIFY(p.send(batch));
        // Do NOT read: actual native pending+whole retained prefixes must stay bounded.
        QTest::qWait(5500);
        auto stats = f.io.statistics();
        QVERIFY(stats.maximumOutput <= 2 * 1024 * 1024);
        QVERIFY(stats.maximumTotalOutput <= 16 * 1024 * 1024);
        QVERIFY(stats.maximumInput <= 16384);
        QVERIFY(stats.maximumOutput > 512 * 1024);
        QCOMPARE(stats.currentClients, 0);
        f.io.stop();
    }
    void eightRealSlowSocketsHaveAggregateBound()
    {
        Fixture f;
        auto& lines = f.doc.lyrics.original.standard.lines;
        lines.clear();
        for (int i = 0; i < 128; ++i)
            lines.append({i, i + 1, QString(1500, QChar(0x96f2)), {}});
        f.doc.revision = 3;
        f.value.lyricsRevision = 3;
        f.value.cursors = domain::LyricCursors{};
        test::observationSummaries(f.value, f.doc.lyrics);
        ++f.value.sequence;
        QVERIFY(f.source->publish(f.value,
                                  std::make_shared<const domain::PlaybackLyricDocument>(f.doc)));
        QCOMPARE(f.start(), LinuxEndpointError::None);
        std::array<std::unique_ptr<Peer>, 8> peers;
        for (auto& p : peers) {
            p = std::make_unique<Peer>();
            QVERIFY(p->connectTo(f.socket()));
            QVERIFY(p->send(request("h", "hello")));
            QByteArray b;
            QTRY_VERIFY(!(b = p->next()).isEmpty());
        }
        auto b = f.value.binding;
        QJsonObject fields{{"instanceId", b.instanceId.toString(QUuid::WithoutBraces)},
                           {"viewEpoch", b.viewEpoch.toString(QUuid::WithoutBraces)},
                           {"trackId", b.trackId.toString(QUuid::WithoutBraces)},
                           {"lyricsRevision", "3"},
                           {"track", "original.standard"},
                           {"fromLine", 0},
                           {"maxLines", 128}};
        QByteArray batch;
        for (int i = 0; i < 3; ++i)
            batch += request(QString("r%1").arg(i), "getLyricsSlice", fields);
        for (auto& p : peers)
            QVERIFY(p->send(batch));
        QTest::qWait(5500);
        const auto stats = f.io.statistics();
        QCOMPARE(stats.maximumClients, 8);
        QCOMPARE(stats.currentClients, 0);
        QVERIFY(stats.maximumOutput <= 2097152);
        QVERIFY(stats.maximumTotalOutput <= 16777216);
        QVERIFY(stats.maximumTotalOutput > 8 * 1024 * 1024);
        QVERIFY(stats.maximumInput <= 16384);
    }
    void queuedWakeCoalescingWithPeriodicPump()
    {
        Fixture f;
        QCOMPARE(f.start(), LinuxEndpointError::None);
        QObject producer;
        QObject::connect(&producer, &QObject::destroyed, &f.io, [] {});
        for (int i = 0; i < 100; ++i) {
            ++f.value.sequence;
            QVERIFY(f.source->publish(f.value));
            if (f.wake->request())
                QMetaObject::invokeMethod(&f.io, &LinuxObservationIo::wake, Qt::QueuedConnection);
        }
        QTRY_COMPARE(f.io.statistics().wakes, quint64(1));
        QVERIFY(!f.wake->pending());
        QTest::qWait(60);
        QCOMPARE(f.io.statistics().wakes, quint64(1));
    }
};
QTEST_GUILESS_MAIN(LinuxObservationIoTest)
#include "linux_observation_io_test.moc"
