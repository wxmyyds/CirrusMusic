#include "linux_observation_io.h"
#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cirrus::infrastructure::ipc
{
namespace
{
class NativeSink final : public IObservationWriteSink
{
  public:
    NativeSink(int descriptor, std::shared_ptr<ObservationMailbox> source)
        : fd(descriptor), mailbox(std::move(source)), fence(mailbox->fence())
    {
    }
    ~NativeSink() override { closeAndDiscard(); }
    qint64 pendingBytes() const override
    {
        if (fd < 0)
            return 0;
        int queued = 0;
        if (::ioctl(fd, TIOCOUTQ, &queued) != 0 || queued < 0)
            return -1;
        if ((lastKernel >= 0 && queued < lastKernel) || (queued == 0 && debt > 0)) {
            if (drained == std::numeric_limits<quint64>::max())
                return -1;
            ++drained;
        }
        lastKernel = queued;
        if (queued == 0)
            debt = 0; // only certain complete kernel drain releases debt
        return debt;
    }
    quint64 drainRevision() const override { return drained; }
    qint64 write(QByteArrayView bytes) override
    {
        if (fd < 0 || mailbox->fence() != fence || pendingBytes() < 0)
            return -1;
        const auto n = ::send(fd, bytes.data(), size_t(bytes.size()), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return 0;
        if (n < 0 || n > PlaybackObservationConnections::ClientBytes - debt)
            return -1;
        debt += n;
        admitted += quint64(n);
        if (pendingBytes() < 0)
            return -1;
        return n;
    }
    void closeAndDiscard() override
    {
        if (read)
            read->setEnabled(false);
        if (writeReady)
            writeReady->setEnabled(false);
        if (fd >= 0)
            ::close(fd);
        fd = -1;
        debt = 0;
    }
    int fd = -1;
    quint64 admitted = 0;
    QSocketNotifier* read = nullptr;
    QSocketNotifier* writeReady = nullptr;

  private:
    std::shared_ptr<ObservationMailbox> mailbox;
    quint64 fence = 0;
    mutable quint64 drained = 0;
    mutable qint64 debt = 0;
    mutable int lastKernel = -1;
};
} // namespace
class LinuxObservationIo::Impl
{
  public:
    struct Client {
        explicit Client(int fd, std::shared_ptr<ObservationMailbox> s) : sink(fd, std::move(s)) {}
        // Notifiers destroyed BEFORE sink; clear its pointers before destruction.
        ~Client() { sink.read = sink.writeReady = nullptr; }
        NativeSink sink;
        std::unique_ptr<QSocketNotifier> read, write;
        std::array<char, 1024> input{};
        qsizetype size = 0, offset = 0;
        quint64 id = 0;
        qint64 inputBorn = 0;
    };
    Impl(LinuxObservationIo& object, std::shared_ptr<ObservationMailbox> s,
         std::shared_ptr<ObservationWakeTicket> w)
        : owner(object), source(std::move(s)), wakeTicket(std::move(w)), engine(source)
    {
        turnTimer = new QTimer(&owner);
        turnTimer->setSingleShot(true);
        deadlineTimer = new QTimer(&owner);
        deadlineTimer->setInterval(25);
        QObject::connect(turnTimer, &QTimer::timeout, &owner, [this] { turn(); });
        QObject::connect(deadlineTimer, &QTimer::timeout, &owner, [this] { schedule(); });
    }
    void schedule()
    {
        if (active && !turnTimer->isActive())
            turnTimer->start(0);
    }
    qint64 now() const { return clock.elapsed(); }
    void stop()
    {
        const bool wasActive = active;
        active = false;
        turnTimer->stop();
        deadlineTimer->stop();
        if (listener)
            listener->setEnabled(false);
        engine.close();
        stats.currentClients = 0;
        for (auto& c : clients)
            c.reset();
        listener.reset();
        endpoint.reset(); // own resources only, I/O thread
        if (wasActive && stopped)
            stopped();
    }
    void collect()
    {
        qsizetype total = 0;
        for (auto& c : clients)
            if (c) {
                if (!engine.isOpen(c->id)) {
                    c.reset();
                    continue;
                }
                const auto input = qsizetype(c->input.size()) + engine.inputBytes(c->id);
                const auto pending = c->sink.pendingBytes();
                const auto output = engine.applicationBytes(c->id) + qMax<qint64>(0, pending);
                stats.maximumInput = qMax(stats.maximumInput, input);
                stats.maximumOutput = qMax(stats.maximumOutput, output);
                total += output;
                if (pending < 0 || input > 16384 ||
                    output > PlaybackObservationConnections::ClientBytes)
                    engine.close(c->id);
            }
        stats.maximumTotalOutput = qMax(stats.maximumTotalOutput, total);
        stats.currentClients = engine.clientCount();
        stats.maximumClients = qMax(stats.maximumClients, stats.currentClients);
    }
    void turn()
    {
        if (!active)
            return;
        if (canceled && canceled()) {
            stop();
            return;
        }
        ++stats.turns;
        const auto latest = source->take();
        if (!endpoint->current() || (latest && latest->snapshot.binding.instanceId != instance)) {
            stop();
            return;
        }
        engine.pump(now()); // <=128 structural records, <=1 request/client
        collect();
        // The endpoint can be discovered before the owner publishes its first view.
        // Keep those peers in the bounded listen backlog until a coherent view exists;
        // opening against a retired mailbox would immediately discard their handshake.
        // <=1 accept/syscall per turn, including overload. Never spin accept to EAGAIN.
        const bool canAccept = latest.has_value();
        const int fd = canAccept ? endpoint->acceptSameUser() : -1;
        if (fd >= 0) {
            stats.maximumAcceptsPerTurn = 1;
            auto slot =
                std::find_if(clients.begin(), clients.end(), [](const auto& c) { return !c; });
            if (slot == clients.end())
                ::close(fd);
            else {
                int sendBytes = 65536;
                if (::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendBytes, sizeof(sendBytes)) != 0)
                    ::close(fd);
                else {
                    auto client = std::make_unique<Client>(fd, source);
                    client->id = engine.open(client->sink, now());
                    if (client->id) {
                        client->read =
                            std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read, &owner);
                        client->write =
                            std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Write, &owner);
                        client->sink.read = client->read.get();
                        client->sink.writeReady = client->write.get();
                        auto attach = [this](QSocketNotifier* n) {
                            QObject::connect(n, &QSocketNotifier::activated, &owner, [this, n] {
                                n->setEnabled(false);
                                schedule();
                            });
                        };
                        attach(client->read.get());
                        attach(client->write.get());
                        client->write->setEnabled(false);
                        *slot = std::move(client);
                    }
                }
            }
        }
        bool again = engine.encodingPending();
        for (auto& c : clients)
            if (c && engine.isOpen(c->id)) {
                if (c->size && now() - c->inputBorn >= 2000) {
                    engine.close(c->id);
                    continue;
                }
                if (!c->size) {
                    const auto n =
                        ::recv(c->sink.fd, c->input.data(), c->input.size(), MSG_DONTWAIT);
                    if (n == 0 ||
                        (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                        engine.close(c->id);
                        continue;
                    }
                    if (n > 0) {
                        c->size = n;
                        c->offset = 0;
                        c->inputBorn = now();
                        stats.receivedBytes += quint64(n);
                    }
                }
                if (c->size) {
                    const auto n = engine.receive(
                        c->id, QByteArrayView(c->input.data() + c->offset, c->size - c->offset),
                        now());
                    c->offset += n;
                    if (c->offset == c->size)
                        c->size = c->offset = 0;
                }
                const auto before = c->sink.admitted;
                engine.write(c->id, now()); // <=64KiB admission, partial frame retained
                const auto after = c->sink.pendingBytes();
                stats.admittedBytes += c->sink.admitted - before;
                if (!engine.isOpen(c->id))
                    continue;
                const bool requests = engine.pendingRequests(c->id) > 0;
                // Pending ids include outgoing responses. Schedule immediate continuation
                // only for buffered INPUT or encoding; readiness/deadline drives output.
                again |= c->size > 0 && !requests;
                c->read->setEnabled(!c->size && !requests);
                c->write->setEnabled(engine.applicationBytes(c->id) > 0 && after >= 0 &&
                                     engine.applicationBytes(c->id) + after <
                                         PlaybackObservationConnections::ClientBytes);
            }
        collect();
        if (listener)
            listener->setEnabled(canAccept);
        if (again)
            schedule();
    }
    LinuxObservationIo& owner;
    std::shared_ptr<ObservationMailbox> source;
    std::shared_ptr<ObservationWakeTicket> wakeTicket;
    PlaybackObservationConnections engine;
    std::array<std::unique_ptr<Client>, 8> clients;
    std::unique_ptr<LinuxObservationEndpoint> endpoint;
    std::unique_ptr<QSocketNotifier> listener;
    QTimer *turnTimer = nullptr, *deadlineTimer = nullptr;
    QElapsedTimer clock;
    LinuxObservationIoStats stats;
    QUuid instance;
    std::function<void()> stopped;
    std::function<bool()> canceled;
    bool active = false, started = false;
};
LinuxObservationIo::LinuxObservationIo(std::shared_ptr<ObservationMailbox> s,
                                       std::shared_ptr<ObservationWakeTicket> w, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, std::move(s), std::move(w)))
{
}
LinuxObservationIo::~LinuxObservationIo() { stop(); }
LinuxEndpointError LinuxObservationIo::start(const LinuxObservationEndpointConfig& config)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto& p = *m_impl;
    if (p.started || !p.source || !p.wakeTicket)
        return LinuxEndpointError::InvalidConfig;
    if (!config.enabled)
        return LinuxEndpointError::Disabled;
    p.started = true;
    LinuxEndpointError e;
    p.endpoint = LinuxObservationEndpoint::create(config, e);
    if (!p.endpoint)
        return e;
    p.instance = config.instanceId;
    p.clock.start();
    p.active = true;
    p.listener = std::make_unique<QSocketNotifier>(p.endpoint->listeningDescriptor(),
                                                   QSocketNotifier::Read, this);
    connect(p.listener.get(), &QSocketNotifier::activated, this, [&p] {
        p.listener->setEnabled(false);
        p.schedule();
    });
    p.deadlineTimer->start();
    p.schedule();
    return LinuxEndpointError::None;
}
void LinuxObservationIo::wake()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_impl->wakeTicket)
        m_impl->wakeTicket->acknowledge();
    ++m_impl->stats.wakes;
    m_impl->schedule();
}
void LinuxObservationIo::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_impl->stop();
}
void LinuxObservationIo::onStopped(std::function<void()> callback)
{
    m_impl->stopped = std::move(callback);
}
void LinuxObservationIo::cancelWhen(std::function<bool()> predicate)
{
    m_impl->canceled = std::move(predicate);
}
bool LinuxObservationIo::running() const { return m_impl->active; }
QString LinuxObservationIo::manifestPath() const
{
    return m_impl->endpoint ? m_impl->endpoint->manifestPath() : QString{};
}
LinuxObservationIoStats LinuxObservationIo::statistics() const { return m_impl->stats; }
} // namespace cirrus::infrastructure::ipc
