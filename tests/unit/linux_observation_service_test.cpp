#include "infrastructure/ipc/linux_observation_client.h"
#include "infrastructure/ipc/linux_observation_service.h"
#include "infrastructure/ipc/playback_observation_codec.h"
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/socket.h>
#include <unistd.h>
using namespace cirrus;
using namespace application;
using namespace domain;
using namespace infrastructure::ipc;
namespace
{
struct Owner {
    BackendFacade facade;
    ListeningSession listening{facade};
    PlaybackObservationPublisher publisher{listening, facade};
    QList<PlaybackCommand> commands;
    QList<MusicCommand> reads;
    Owner()
    {
        QObject::connect(&facade, &BackendFacade::playbackRequested, &facade,
                         [this](PlaybackCommand c) {
                             commands.append(c);
                             QTimer::singleShot(0, &facade, [this, c] {
                                 BackendEvent e;
                                 e.kind = BackendEvent::Kind::Acknowledged;
                                 e.commandId = c.id;
                                 e.accepted = true;
                                 facade.deliver(e);
                             });
                         });
        QObject::connect(&facade, &BackendFacade::musicRequested, &facade,
                         [this](MusicCommand c) { reads.append(c); });
        facade.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        facade.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest, {}};
        facade.deliver(e);
    }
    void load()
    {
        listening.replaceQueue({{"1"}});
        state();
        SongLyrics l;
        l.song = {"1"};
        l.original.standard.availability = LyricAvailability::Present;
        l.original.standard.timing = LyricTiming::Lines;
        l.original.standard.lines = {{0, 10000, "synthetic 雲", {}}};
        QVERIFY(!reads.isEmpty());
        const auto c = reads.last();
        facade.deliverMusic({c.id, c.generation, c.context, l});
    }
    void state()
    {
        PlaybackCommand c;
        for (auto it = commands.crbegin(); it != commands.crend(); ++it)
            if (it->action == PlaybackAction::Play) {
                c = *it;
                break;
            }
        facade.deliverPlaybackSource({c.token, c.query.song, SourceProvider::Netease, {}});
        PlaybackSnapshot p;
        p.token = c.token;
        p.song = c.query.song;
        p.state = PlaybackState::Playing;
        p.intent = TransportIntent::Play;
        p.positionMs = 10;
        p.durationMs = 10000;
        p.seekable = true;
        p.positionRevision = 1;
        facade.deliverPlayback({c.generation, p});
    }
    void phase(SessionPhase phase)
    {
        BackendEvent e;
        e.kind = BackendEvent::Kind::Session;
        e.session = facade.snapshot();
        e.session.phase = phase;
        facade.deliver(e);
    }
};
struct Peer {
    int fd = -1;
    QByteArray bytes;
    ~Peer()
    {
        if (fd >= 0)
            ::close(fd);
    }
    bool send(QString id, QString method)
    {
        auto b = PlaybackObservationCodec::frame(
            QJsonDocument(
                QJsonObject{{"v", 1}, {"kind", "request"}, {"id", id}, {"method", method}})
                .toJson(QJsonDocument::Compact));
        return ::send(fd, b.constData(), size_t(b.size()), MSG_NOSIGNAL | MSG_DONTWAIT) == b.size();
    }
    bool frame()
    {
        std::array<char, 4096> b{};
        for (int i = 0; i < 8; ++i) {
            auto n = ::recv(fd, b.data(), b.size(), MSG_DONTWAIT);
            if (n <= 0)
                break;
            bytes.append(b.data(), n);
        }
        if (bytes.size() < 4)
            return false;
        quint32 n = 0;
        for (int i = 0; i < 4; ++i)
            n = (n << 8) | uchar(bytes[i]);
        return n <= PlaybackObservationCodec::FrameBytes && bytes.size() >= 4 + n;
    }
    QJsonObject body() const { return QJsonDocument::fromJson(bytes.mid(4)).object(); }
    bool eof()
    {
        char c;
        return ::recv(fd, &c, 1, MSG_DONTWAIT | MSG_PEEK) == 0;
    }
};
} // namespace
class LinuxObservationServiceTest final : public QObject
{
    Q_OBJECT
  private slots:
    void defaultOffNoEffects()
    {
        Owner h;
        QTemporaryDir root{QDir::tempPath() + "/obs-XXXXXX"};
        LinuxObservationService service(h.publisher);
        QVERIFY(!service.start({}));
        QCOMPARE(service.state(), LinuxObservationServiceState::Disabled);
        QVERIFY(service.shutdownTicket()->complete.load());
        QVERIFY(QDir(root.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
        QVERIFY(h.commands.isEmpty());
        QVERIFY(h.reads.isEmpty());
    }
    void ownerThreadCompositionAndVerifiedHello()
    {
        Owner h;
        h.load();
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        const auto old = h.publisher.snapshot().binding;
        const auto commands = h.commands.size(), reads = h.reads.size();
        QTest::qWait(3);
        QTemporaryDir root{QDir::tempPath() + "/obs-XXXXXX"};
        LinuxObservationService service(h.publisher);
        QVERIFY(service.start({true, root.path(), "ob-service-test"}));
        QTRY_COMPARE(service.state(), LinuxObservationServiceState::Running);
        QTRY_VERIFY(h.publisher.validatedExport().has_value());
        const auto binding = h.publisher.snapshot().binding;
        QVERIFY(binding.instanceId != old.instanceId);
        QVERIFY(!h.publisher.snapshot().positionObservedMonoMs);
        LinuxObservationClientError error;
        auto client = LinuxObservationClient::connect(
            {root.path(), "ob-service-test", binding.instanceId}, error);
        QVERIFY(client);
        QCOMPARE(error, LinuxObservationClientError::None);
        QTRY_COMPARE(client->advance(), LinuxObservationClientState::Verified);
        Peer peer;
        peer.fd = client->takeVerifiedDescriptor();
        QVERIFY(peer.fd >= 0);
        QVERIFY(peer.send("s", "getSnapshot"));
        QTRY_VERIFY(peer.frame());
        auto snapshot = peer.body()["snapshot"].toObject();
        QCOMPARE(snapshot["instanceId"].toString(),
                 binding.instanceId.toString(QUuid::WithoutBraces));
        QCOMPARE(snapshot["track"].toObject()["providerSongId"].toString(), QString("1"));
        QCOMPARE(h.commands.size(), commands);
        QCOMPARE(h.reads.size(), reads);
        peer.bytes.clear();
        const auto path = service.manifestPath();
        auto done = service.shutdownTicket();
        service.close();
        QVERIFY(!h.publisher.validatedExport());
        QTRY_VERIFY(done->complete.load());
        QTRY_COMPARE(service.state(), LinuxObservationServiceState::Stopped);
        QVERIFY(!QFileInfo::exists(path));
        QTRY_VERIFY(peer.eof());
        QVERIFY(!service.start({true, root.path(), "ob-service-test"}));
        auto stats = service.finalStatistics();
        QVERIFY(stats.maximumInput <= 16384);
        QVERIFY(stats.maximumOutput <= 2097152);
        QVERIFY(stats.wakes > 0);
    }
    void privateBarrierClosesRealClientWithoutStoppingOwner()
    {
        Owner h;
        h.load();
        QTRY_VERIFY(h.publisher.snapshot().song.has_value());
        QTemporaryDir root{QDir::tempPath() + "/obs-XXXXXX"};
        LinuxObservationService service(h.publisher);
        QVERIFY(service.start({true, root.path(), "ob-service-test"}));
        QTRY_COMPARE(service.state(), LinuxObservationServiceState::Running);
        LinuxObservationClientError error;
        auto client = LinuxObservationClient::connect(
            {root.path(), "ob-service-test", h.publisher.snapshot().binding.instanceId}, error);
        QVERIFY(client);
        QTRY_COMPARE(client->advance(), LinuxObservationClientState::Verified);
        Peer peer;
        peer.fd = client->takeVerifiedDescriptor();
        QVERIFY(peer.fd >= 0);
        h.phase(SessionPhase::Authenticating);
        QVERIFY(!h.publisher.validatedExport());
        QTRY_VERIFY(peer.eof());
        h.phase(SessionPhase::Guest);
        QTest::qWait(40);
        QVERIFY(!h.publisher.validatedExport());
        QVERIFY(h.listening.queue().active.isNull()); // a phase change resets the queue
        QVERIFY(h.listening.replaceQueue({{"1"}}));
        h.state();
        QTRY_VERIFY(h.publisher.validatedExport().has_value());
        service.close();
        QTRY_VERIFY(service.shutdownTicket()->complete.load());
    }
    void closeBeforeStartupCannotLeaveEndpoint()
    {
        Owner h;
        QTemporaryDir root{QDir::tempPath() + "/obs-XXXXXX"};
        LinuxObservationService service(h.publisher);
        QVERIFY(service.start({true, root.path(), "ob-service-test"}));
        service.close();
        QTRY_VERIFY(service.shutdownTicket()->complete.load());
        QTRY_COMPARE(service.state(), LinuxObservationServiceState::Stopped);
        QVERIFY(QDir(root.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }
    void destroyedOwnerNonblockingCleanup()
    {
        Owner h;
        QTemporaryDir root{QDir::tempPath() + "/obs-XXXXXX"};
        auto service = std::make_unique<LinuxObservationService>(h.publisher);
        QVERIFY(service->start({true, root.path(), "ob-service-test"}));
        QTRY_COMPARE(service->state(), LinuxObservationServiceState::Running);
        const auto path = service->manifestPath();
        auto done = service->shutdownTicket();
        service.reset();
        QTRY_VERIFY(done->complete.load());
        QVERIFY(!QFileInfo::exists(path));
    }
    void unsafeRootFailureAndClosedPublisher()
    {
        Owner h;
        QTemporaryDir root{QDir::tempPath() + "/obs-XXXXXX"};
        QVERIFY(QFile::setPermissions(root.path(), QFile::ReadOwner | QFile::WriteOwner |
                                                       QFile::ExeOwner | QFile::ReadGroup |
                                                       QFile::ExeGroup));
        LinuxObservationService service(h.publisher);
        QVERIFY(service.start({true, root.path(), "ob-service-test"}));
        QTRY_COMPARE(service.state(), LinuxObservationServiceState::Failed);
        QTRY_VERIFY(service.shutdownTicket()->complete.load());
        QVERIFY(service.manifestPath().isEmpty());
        h.publisher.close();
        LinuxObservationService other(h.publisher);
        QVERIFY(!other.start({true, root.path(), "ob-service-test"}));
        QCOMPARE(other.state(), LinuxObservationServiceState::Failed);
    }
    void instanceRestartDiscardsNestedCaptureAssociation()
    {
        Owner h;
        h.load();
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        bool once = false, authorized = false;
        QObject::connect(&h.publisher, &PlaybackObservationPublisher::contextRetired, &h.publisher,
                         [&](ObservationBinding) {
                             if (once)
                                 return;
                             once = true;
                             QCoreApplication::processEvents();
                             authorized = h.publisher.validatedExport().has_value();
                         });
        QTemporaryDir root{QDir::tempPath() + "/obs-XXXXXX"};
        LinuxObservationService service(h.publisher);
        QVERIFY(service.start({true, root.path(), "ob-service-test"}));
        QVERIFY(once);
        QVERIFY(!authorized);
        QTRY_COMPARE(service.state(), LinuxObservationServiceState::Running);
        QTRY_COMPARE(h.publisher.snapshot().documentStatus, ObservationContentStatus::Ready);
        auto value = h.publisher.validatedExport();
        QVERIFY(value && value->document);
        QCOMPARE(value->document->binding, value->snapshot.binding);
        QCOMPARE(value->document->revision, value->snapshot.lyricsRevision);
        service.close();
        QTRY_VERIFY(service.shutdownTicket()->complete.load());
    }
    void instanceRestartDoesNotUndoPrivateRetirement()
    {
        Owner h;
        h.load();
        QTRY_VERIFY(h.publisher.snapshot().song.has_value());
        h.publisher.retireExports();
        QTemporaryDir root{QDir::tempPath() + "/obs-XXXXXX"};
        LinuxObservationService service(h.publisher);
        QVERIFY(service.start({true, root.path(), "ob-service-test"}));
        QTRY_COMPARE(service.state(), LinuxObservationServiceState::Running);
        QTest::qWait(40);
        QVERIFY(!h.publisher.validatedExport());
        QVERIFY(h.listening.select(h.listening.queue().active));
        h.state();
        QTRY_VERIFY(h.publisher.validatedExport().has_value());
        service.close();
        QTRY_VERIFY(service.shutdownTicket()->complete.load());
    }
    void xdgSelectionHasNoFallback()
    {
        struct Restore {
            QByteArray old = qgetenv("XDG_RUNTIME_DIR");
            bool set = qEnvironmentVariableIsSet("XDG_RUNTIME_DIR");
            ~Restore()
            {
                if (set)
                    qputenv("XDG_RUNTIME_DIR", old);
                else
                    qunsetenv("XDG_RUNTIME_DIR");
            }
        } restore;
        qunsetenv("XDG_RUNTIME_DIR");
        auto missing = LinuxObservationServiceConfig::fromXdg(true, "ob-service-test");
        QVERIFY(missing.enabled);
        QVERIFY(missing.runtimeRoot.isEmpty());
        Owner h;
        LinuxObservationService failure(h.publisher);
        QVERIFY(failure.start(missing));
        QTRY_COMPARE(failure.state(), LinuxObservationServiceState::Failed);
        QTRY_VERIFY(failure.shutdownTicket()->complete.load());
        QTemporaryDir root{QDir::tempPath() + "/obs-XXXXXX"};
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(root.path()));
        auto valid = LinuxObservationServiceConfig::fromXdg(true, "ob-service-test");
        QCOMPARE(valid.runtimeRoot, root.path());
        LinuxObservationService service(h.publisher);
        QVERIFY(service.start(valid));
        QTRY_COMPARE(service.state(), LinuxObservationServiceState::Running);
        service.close();
        QTRY_VERIFY(service.shutdownTicket()->complete.load());
        auto off = LinuxObservationServiceConfig::fromXdg(false, "ob-service-test");
        QVERIFY(!off.enabled);
        QVERIFY(off.runtimeRoot.isEmpty());
    }
    void repeatedThreadShutdownHasNoDescriptors()
    {
        const auto before =
            QDir("/proc/self/fd").entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size();
        Owner h;
        QTemporaryDir root{QDir::tempPath() + "/obs-XXXXXX"};
        for (int i = 0; i < 8; ++i) {
            LinuxObservationService service(h.publisher);
            QVERIFY(service.start({true, root.path(), "ob-service-test"}));
            QTRY_COMPARE(service.state(), LinuxObservationServiceState::Running);
            service.close();
            QTRY_VERIFY(service.shutdownTicket()->complete.load());
            QTRY_COMPARE(service.state(), LinuxObservationServiceState::Stopped);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        const auto after =
            QDir("/proc/self/fd").entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size();
        QVERIFY(after <= before + 1);
    }
};
QTEST_GUILESS_MAIN(LinuxObservationServiceTest)
#include "linux_observation_service_test.moc"
