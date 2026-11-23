#include "collection_writes.h"

#include "application/backend/backend_facade.h"
#include "logging/log.h"

#include <type_traits>

namespace cirrus::ui
{
namespace
{

// The facade said 0 although ready, signed in and the change valid: full, or the
// change's source read is from another session. A moment later, a few times.
constexpr int kRetryMs = 200;
constexpr int kRetries = 5;

const char* changeName(const domain::CollectionChange& change)
{
    return std::visit(
        [](const auto& c) -> const char* {
            using C = std::remove_cvref_t<decltype(c)>;
            using namespace domain;
            if constexpr (std::is_same_v<C, SongHeart>)
                return "song_heart";
            else if constexpr (std::is_same_v<C, PlaylistCollection>)
                return "playlist_songs";
            else if constexpr (std::is_same_v<C, AlbumCollection>)
                return "album";
            else if constexpr (std::is_same_v<C, ArtistCollection>)
                return "artist";
            else if constexpr (std::is_same_v<C, PodcastSubscription>)
                return "podcast";
            else if constexpr (std::is_same_v<C, UserFollow>)
                return "user";
            else if constexpr (std::is_same_v<C, PlaylistSubscription>)
                return "playlist";
            else if constexpr (std::is_same_v<C, PlaylistCreate>)
                return "playlist_create";
            else if constexpr (std::is_same_v<C, PlaylistRename>)
                return "playlist_rename";
            else if constexpr (std::is_same_v<C, PlaylistDescription>)
                return "playlist_description";
            else if constexpr (std::is_same_v<C, PlaylistDelete>)
                return "playlist_delete";
            else if constexpr (std::is_same_v<C, PlaylistSongOrder>)
                return "playlist_order";
            else
                return "other";
        },
        change);
}

} // namespace

CollectionWrites::CollectionWrites(application::BackendFacade& facade, QObject* parent)
    : QObject(parent), m_facade(&facade)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(kRetryMs);
    connect(&m_retry, &QTimer::timeout, this, &CollectionWrites::pump);
    connect(&facade, &application::BackendFacade::stateChanged, this,
            &CollectionWrites::schedulePump);
    connect(&facade, &application::BackendFacade::sessionChanged, this,
            &CollectionWrites::schedulePump);
    connect(&facade, &application::BackendFacade::collectionCompleted, this,
            [this](quint64 id, domain::SessionToken, const domain::CollectionResult& result) {
                completed(id, result);
            });
}

// Writes still running at exit are left to the facade (it settles them as Unknown).
CollectionWrites::~CollectionWrites() = default;

std::optional<failure::Kind> CollectionWrites::kindOf(const domain::CollectionResult& result)
{
    using domain::CollectionError;
    using domain::CollectionOutcome;
    if (result.outcome == CollectionOutcome::Acknowledged)
        return std::nullopt;
    if (result.outcome == CollectionOutcome::Unknown)
        return failure::Unknown; // sent; whether it took is not known
    switch (result.error) {
    case CollectionError::Network:
        return failure::Network;
    case CollectionError::Timeout:
        return failure::Timeout;
    case CollectionError::Busy:
        return failure::Busy;
    case CollectionError::RateLimited:
        return failure::RateLimited;
    case CollectionError::AuthenticationRequired:
    case CollectionError::StaleContext: // not sent: the account changed
        return failure::SignedOut;
    case CollectionError::InvalidRequest:
        return failure::Invalid;
    case CollectionError::ProviderRejected:
        // The service says "too often" (405) as a rejection.
        return result.providerCode == 405 ? failure::RateLimited : failure::Rejected;
    case CollectionError::InvalidResponse:
    case CollectionError::ProtocolFailure:
        return failure::Rejected;
    case CollectionError::None:
    case CollectionError::Canceled:
        break;
    }
    return failure::Unknown;
}

Request CollectionWrites::submit(QList<Step> steps, Done done, Fail fail)
{
    const quint64 ticket = ++m_nextTicket;
    QString account;
    if (m_facade && m_facade->snapshot().phase == domain::SessionPhase::Authenticated)
        account = m_facade->snapshot().accountId;
    m_waiting.push_back(Job{ticket, account, std::move(steps), std::move(done), std::move(fail)});
    schedulePump();
    return Request([self = QPointer<CollectionWrites>(this), ticket]() {
        if (!self)
            return;
        // Only the reply goes; the write is still made.
        auto drop = [](Job& job) {
            job.done = {};
            job.fail = {};
        };
        if (self->m_running && self->m_running->ticket == ticket)
            drop(*self->m_running);
        for (Job& job : self->m_waiting)
            if (job.ticket == ticket)
                drop(job);
    });
}

bool CollectionWrites::closed() const
{
    if (!m_facade)
        return true;
    const auto state = m_facade->state();
    return state == application::BackendState::Failed ||
           state == application::BackendState::Stopping ||
           state == application::BackendState::Stopped;
}

void CollectionWrites::schedulePump()
{
    if (m_pumpScheduled)
        return;
    m_pumpScheduled = true;
    QMetaObject::invokeMethod(this, &CollectionWrites::pump, Qt::QueuedConnection);
}

void CollectionWrites::finish(std::optional<failure::Kind> kind,
                              const domain::CollectionResult& result)
{
    Job job = std::move(*m_running);
    m_running.reset();
    m_runningId = 0;
    schedulePump();
    if (!kind) {
        if (job.done)
            job.done(result);
    } else if (job.fail) {
        job.fail(*kind, result.retryAfterMs);
    }
}

void CollectionWrites::pump()
{
    m_pumpScheduled = false;
    if (closed()) {
        if (m_running && m_runningId == 0)
            finish(failure::Unknown, {});
        auto waiting = std::exchange(m_waiting, {});
        for (Job& job : waiting)
            if (job.fail)
                job.fail(failure::Unknown, std::nullopt);
        return;
    }
    if (!m_running) {
        if (m_waiting.empty())
            return;
        m_running = std::move(m_waiting.front());
        m_waiting.pop_front();
        m_retries = 0;
    }
    if (m_runningId != 0)
        return; // a step is with the facade
    if (m_facade->state() != application::BackendState::Ready)
        return; // starting: once ready
    const auto session = m_facade->snapshot();
    if (m_running->accountId.isEmpty() || session.phase != domain::SessionPhase::Authenticated ||
        session.accountId != m_running->accountId) {
        log::info(lcUiBackend, "write.dropped", {"reason", "account"});
        finish(failure::SignedOut, {});
        return;
    }
    if (m_running->steps.isEmpty()) {
        finish(failure::Invalid, {});
        return;
    }
    auto change = m_running->steps.constFirst()(*m_facade);
    if (!change) {
        log::warn(lcUiBackend, "write.invalid");
        finish(failure::Invalid, {});
        return;
    }
    const char* name = changeName(*change);
    const auto id = m_facade->mutateCollection({QUuid::createUuid(), std::move(*change)});
    if (id == 0) {
        if (++m_retries <= kRetries) {
            m_retry.start();
            return;
        }
        log::warn(lcUiBackend, "write.refused", {"change", name});
        finish(failure::Busy, {});
        return;
    }
    m_runningId = id;
    m_runningName = name;
}

void CollectionWrites::completed(quint64 id, const domain::CollectionResult& result)
{
    if (!m_running || id != m_runningId)
        return;
    m_runningId = 0;
    const auto kind = kindOf(result);
    if (kind) {
        log::warn(lcUiBackend, "write.failed",
                  {{"change", m_runningName},
                   {"outcome", result.outcome},
                   {"err", result.error},
                   {"code", result.providerCode.value_or(0)}});
        finish(kind, result);
        return;
    }
    log::info(lcUiBackend, "write.done", {"change", m_runningName});
    m_running->steps.removeFirst();
    if (m_running->steps.isEmpty())
        finish(std::nullopt, result);
    else
        schedulePump();
}

} // namespace cirrus::ui
