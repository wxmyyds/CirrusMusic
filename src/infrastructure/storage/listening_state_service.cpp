#include "listening_state_service.h"
#include "logging/log.h"

#include <QPointer>
#include <QThread>
#include <limits>

namespace cirrus::storage
{
namespace
{
bool sameQueue(const domain::QueueSnapshot& a, const domain::QueueSnapshot& b)
{
    if (a.active != b.active || a.order != b.order || a.repeat != b.repeat ||
        a.entries.size() != b.entries.size())
        return false;
    for (qsizetype i = 0; i < a.entries.size(); ++i)
        if (a.entries[i].id != b.entries[i].id || a.entries[i].song != b.entries[i].song)
            return false;
    return true;
}
bool eligible(const domain::SessionSnapshot& snapshot)
{
    return snapshot.token.isValid() && domain::validListeningAccount(snapshot.accountId) &&
           ((snapshot.phase == domain::SessionPhase::Guest && snapshot.accountId.isEmpty()) ||
            (snapshot.phase == domain::SessionPhase::Authenticated &&
             !snapshot.accountId.isEmpty()));
}
} // namespace
ListeningStateService::ListeningStateService(application::ListeningSession& listening,
                                             application::BackendFacade& facade, QObject* parent)
    : QObject(parent), m_listening(listening), m_facade(facade)
{
    Q_ASSERT(listening.thread() == thread() && facade.thread() == thread());
    m_timer.setSingleShot(true);
    m_timer.setInterval(500);
    m_historyTimer.setSingleShot(true);
    m_historyTimer.setInterval(5000);
    connect(&m_historyTimer, &QTimer::timeout, this, &ListeningStateService::flushHistory);
    m_songTimer.setSingleShot(true);
    connect(&m_songTimer, &QTimer::timeout, this, &ListeningStateService::requestMissingSongs);
    connect(&listening, &application::ListeningSession::listeningRecordChanged, this,
            &ListeningStateService::recordChanged, Qt::DirectConnection);
    connect(&listening, &application::ListeningSession::historyCleared, this,
            &ListeningStateService::clearHistory, Qt::DirectConnection);
    connect(
        &listening, &application::ListeningSession::historyStorageOverflow, this,
        [this] {
            m_writeRejected = true;
            emit storageFailed(StorageError::Query);
        },
        Qt::DirectConnection);
    connect(&m_timer, &QTimer::timeout, this, &ListeningStateService::flush);
    connect(
        &facade, &application::BackendFacade::sessionChanged, this, [this] { contextChanged(); },
        Qt::DirectConnection);
    connect(
        &facade, &application::BackendFacade::stateChanged, this, [this] { contextChanged(); },
        Qt::DirectConnection);
    connect(&listening, &application::ListeningSession::queueChanged, this,
            &ListeningStateService::queueChanged, Qt::DirectConnection);
    connect(&listening, &application::ListeningSession::fmChanged, this,
            &ListeningStateService::fmChanged, Qt::DirectConnection);
    connect(
        &listening, &application::ListeningSession::queueCleared, this,
        [this](domain::SessionSnapshot source) {
            if (live() && source == m_source) {
                m_loadId = 0; // even an already-empty explicit clear defeats a late restore
                m_save = ListeningStateJob{0, ListeningStateOperation::SaveQueue, source.accountId,
                                           m_listening.queue()};
                flush();
            }
        },
        Qt::DirectConnection);
}
bool ListeningStateService::live() const
{
    return !m_closed && m_facade.state() == application::BackendState::Ready &&
           eligible(m_source) && m_facade.snapshot() == m_source;
}
void ListeningStateService::contextChanged()
{
    if (m_closed)
        return;
    const auto source = m_facade.snapshot();
    if (m_facade.state() != application::BackendState::Ready || !eligible(source)) {
        m_loadId = 0;
        return;
    }
    if (source == m_source)
        return;
    const QPointer<ListeningStateService> guard(this);
    flush(); // old account's captured edit, not the reset of the new account
    if (!guard || m_closed || m_facade.state() != application::BackendState::Ready ||
        m_facade.snapshot() != source)
        return;
    m_source = source;
    ++m_historyEpoch; // pending page data is never delivered into a different live account
    m_loadId = 0;
    m_songsAsked.clear();
    scheduleSongs(15000); // details missing from earlier runs, after start-up settles
    if (!m_listening.queue().entries.isEmpty() ||
        m_listening.mode() != domain::ListeningMode::Queue)
        return;
    if (m_pending.size() >= 7 || m_nextId == std::numeric_limits<quint64>::max()) {
        emit storageFailed(StorageError::Query);
        return;
    }
    m_restoreIntent = m_listening.intentRevision();
    m_loadId = ++m_nextId;
    m_pending.insert(m_loadId);
    emit requested({m_loadId, ListeningStateOperation::LoadQueue, source.accountId, {}});
}
void ListeningStateService::queueChanged(const domain::QueueSnapshot& queue)
{
    if (!live() || m_listening.mode() != domain::ListeningMode::Queue ||
        m_listening.queueContext() != m_source.token || !domain::validSavedQueue(queue) ||
        !sameQueue(queue, m_listening.queue()))
        return;
    if (m_restoring)
        return;
    m_loadId = 0; // append/remove/modes must win even if they don't alter playback revision
    m_save = ListeningStateJob{0, ListeningStateOperation::SaveQueue, m_source.accountId, queue};
    if (m_listening.restoredFm())
        m_save->mode = domain::ListeningMode::PersonalFm;
    if (!m_timer.isActive())
        m_timer.start();
}
void ListeningStateService::fmChanged()
{
    // Personal FM's songs so far, once it has one, replace the saved queue.
    if (!live() || m_listening.mode() != domain::ListeningMode::PersonalFm ||
        m_listening.queueContext() != m_source.token || m_restoring)
        return;
    const auto songs = m_listening.fmQueue();
    if (songs.entries.isEmpty() || !domain::validSavedQueue(songs))
        return;
    m_loadId = 0;
    m_save = ListeningStateJob{0, ListeningStateOperation::SaveQueue, m_source.accountId, songs};
    m_save->mode = domain::ListeningMode::PersonalFm;
    if (!m_timer.isActive())
        m_timer.start();
}
void ListeningStateService::flush()
{
    m_timer.stop();
    if (!m_save)
        return;
    if (m_pending.size() >= 8 || m_nextId == std::numeric_limits<quint64>::max()) {
        m_writeRejected = true;
        emit storageFailed(StorageError::Query);
        return; // retain dirty state; never claim a rejected write was saved
    }
    auto job = std::move(*m_save);
    m_save.reset();
    job.id = ++m_nextId;
    m_pending.insert(job.id);
    emit requested(std::move(job));
}
void ListeningStateService::deliver(ListeningStateResult result)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_pending.remove(result.id))
        return;
    // The guest's account is empty: tell "a guest write" from "not a write" by presence.
    const auto scope = m_clearRequests.contains(result.id)   ? m_clearRequests.take(result.id)
                       : m_historyWrites.contains(result.id) ? m_historyWrites.take(result.id)
                                                             : std::optional<QString>{};
    if (result.error == StorageError::None && live() && scope && *scope == m_source.accountId) {
        const QPointer<ListeningStateService> alive(this);
        emit storedHistoryChanged();
        if (!alive)
            return;
    }
    const QPointer<ListeningStateService> guard(this);
    if (m_historyReads.contains(result.id)) {
        const auto read = m_historyReads.take(result.id);
        auto error = result.error;
        if (!live() || read.source != m_source || read.epoch != m_historyEpoch)
            error = StorageError::NotOpen;
        else if (error == StorageError::None && (read.stats ? !result.stats : !result.history))
            error = StorageError::Corrupt;
        if (read.stats)
            emit statsReady(result.id, error,
                            error == StorageError::None ? std::move(*result.stats)
                                                        : domain::ListeningStats{});
        else
            emit historyPageReady(result.id, error,
                                  error == StorageError::None ? std::move(*result.history)
                                                              : domain::ListeningHistoryPage{});
        if (guard && !m_closed && !m_historyTimer.isActive())
            flushHistory();
        return;
    }
    const bool songsCurrent = live() && m_songSource == m_source && m_songEpoch == m_historyEpoch;
    if (result.id == m_songRead) {
        m_songRead = 0;
        // The same songs as last time: they could not be found; stop until history changes.
        if (!m_closed && result.error == StorageError::None && songsCurrent &&
            !result.missingSongs.isEmpty() && result.missingSongs != m_songsAsked) {
            m_songsAsked = result.missingSongs;
            m_songTicket = ++m_nextTicket;
            log::debug(lcStorage, "listening.songs_wanted", {"count", result.missingSongs.size()});
            emit songsWanted(m_songTicket, result.missingSongs);
        }
        if (guard && !m_closed && !m_historyTimer.isActive())
            flushHistory();
        return;
    }
    if (result.id == m_songSave) {
        m_songSave = 0;
        if (result.error != StorageError::None) {
            log::warn(lcStorage, "listening.songs_save_failed", {"err", result.error});
            emit storageFailed(result.error);
        } else if (songsCurrent) {
            emit storedHistoryChanged();
            if (!guard)
                return;
            scheduleSongs(0); // the next batch
        }
        if (guard && !m_closed && !m_historyTimer.isActive())
            flushHistory();
        return;
    }
    if (result.error != StorageError::None)
        emit storageFailed(result.error);
    else
        scheduleSongs(3000); // a record may have a new song
    if (!guard)
        return;
    if (!m_closed && result.id == m_loadId) {
        m_loadId = 0;
        if (result.error == StorageError::None && result.queue && live()) {
            const auto source = m_source;
            m_restoring = true;
            const bool applied =
                m_listening.restoreQueue(*result.queue, source, m_restoreIntent, result.mode);
            if (!guard)
                return;
            m_restoring = false;
            // Native/direct signal observers may synchronously edit during restoration.
            if (live() && m_listening.queueContext() == source.token &&
                !sameQueue(m_listening.queue(), *result.queue))
                queueChanged(m_listening.queue());
            if (!guard)
                return;
            emit queueRestored(applied);
        }
    }
    if (guard && !m_closed && m_save)
        flush();
    if (guard && !m_closed && !m_historyTimer.isActive())
        flushHistory();
}
void ListeningStateService::recordChanged(domain::StoredListeningRecord record)
{
    // Podcast programs are not kept: the local history and its statistics are about songs
    // (a program's song id is only its track). They are still reported.
    if (m_closed || !record.program.value.isEmpty())
        return;
    const auto existing = m_historyDirty.constFind(record.id);
    const bool changedIdentity =
        existing != m_historyDirty.cend() &&
        (existing->accountId != record.accountId || existing->song != record.song ||
         existing->startedAtMs != record.startedAtMs || existing->source != record.source ||
         existing->queueEntry != record.queueEntry);
    if (!domain::validStoredListeningRecord(record) || changedIdentity ||
        (!m_historyDirty.contains(record.id) && m_historyDirty.size() >= 256)) {
        m_writeRejected = true;
        emit storageFailed(StorageError::Query);
        return;
    }
    const bool finalized = record.end != domain::ListeningEnd::Active;
    m_historyDirty.insert(record.id, std::move(record));
    if (finalized)
        flushHistory();
    else if (!m_historyTimer.isActive())
        m_historyTimer.start();
}
void ListeningStateService::clearHistory(domain::SessionSnapshot source)
{
    if (m_closed || !eligible(source))
        return;
    ++m_historyEpoch; // cancels pre-clear reads as well as deferred writes
    for (auto it = m_historyDirty.begin(); it != m_historyDirty.end();)
        if (it->accountId == source.accountId)
            it = m_historyDirty.erase(it);
        else
            ++it;
    if (!m_historyClears.contains(source.accountId) && m_historyClears.size() >= 8) {
        m_writeRejected = true;
        emit storageFailed(StorageError::Query);
        return;
    }
    m_historyClears.insert(source.accountId);
    flushHistory(); // FIFO erase BEFORE any subsequently observed new load; no requeue on ack
}
QList<ListeningStateJob> ListeningStateService::historyJobs(qsizetype maximumJobs)
{
    QList<ListeningStateJob> jobs;
    while (jobs.size() < maximumJobs && (!m_historyClears.isEmpty() || !m_historyDirty.isEmpty())) {
        if (m_nextId == std::numeric_limits<quint64>::max()) {
            m_writeRejected = true;
            break;
        }
        ListeningStateJob job;
        job.id = ++m_nextId;
        if (!m_historyClears.isEmpty()) {
            job.operation = ListeningStateOperation::ClearHistory;
            job.accountId = *m_historyClears.begin();
            m_historyClears.remove(job.accountId);
            m_clearRequests.insert(job.id, job.accountId);
        } else {
            job.operation = ListeningStateOperation::SaveHistory;
            job.accountId = m_historyDirty.first().accountId;
            m_historyWrites.insert(job.id, job.accountId);
            for (auto it = m_historyDirty.begin();
                 it != m_historyDirty.end() && job.records.size() < 64;)
                if (it->accountId == job.accountId) {
                    job.records.append(std::move(it.value()));
                    it = m_historyDirty.erase(it);
                } else
                    ++it;
        }
        m_pending.insert(job.id);
        jobs.append(std::move(job));
    }
    return jobs;
}
void ListeningStateService::flushHistory()
{
    if (m_closed)
        return;
    m_historyTimer.stop();
    const QPointer<ListeningStateService> guard(this);
    while (m_pending.size() <
           7) { // leave an admission slot; close handoff is independently drained
        auto jobs = historyJobs(1);
        if (jobs.isEmpty())
            break;
        emit requested(std::move(jobs.first()));
        if (!guard || m_closed)
            return;
    }
    if ((!m_historyDirty.isEmpty() || !m_historyClears.isEmpty()) && !m_historyTimer.isActive())
        m_historyTimer.start();
}
quint64 ListeningStateService::historyPage(int pageSize,
                                           std::optional<domain::ListeningHistoryCursor> cursor)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!live() || m_historyClears.contains(m_source.accountId) ||
        m_clearRequests.values().contains(m_source.accountId) || pageSize < 1 || pageSize > 100 ||
        m_pending.size() >= 6 || m_historyReads.size() >= 2 ||
        m_nextId == std::numeric_limits<quint64>::max() ||
        (cursor && (cursor->id.isNull() || cursor->startedAtMs <= 0 ||
                    cursor->startedAtMs > 253402300799999LL))) {
        emit storageFailed(StorageError::Query);
        return 0;
    }
    ListeningStateJob job;
    const auto id = job.id = ++m_nextId;
    job.operation = ListeningStateOperation::LoadHistory;
    job.accountId = m_source.accountId;
    job.cursor = cursor;
    job.pageSize = pageSize;
    m_pending.insert(id);
    m_historyReads.insert(id, {m_source, m_historyEpoch});
    emit requested(std::move(job));
    return id;
}
quint64 ListeningStateService::stats(domain::ListeningStatsQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (live() && !m_historyClears.contains(m_source.accountId) &&
        !m_clearRequests.values().contains(m_source.accountId))
        flushHistory(); // FIFO on the database thread: the records just heard count
    if (!live() || m_historyClears.contains(m_source.accountId) ||
        m_clearRequests.values().contains(m_source.accountId) || !query.isValid() ||
        m_pending.size() >= 7 || m_historyReads.size() >= 2 ||
        m_nextId == std::numeric_limits<quint64>::max()) {
        emit storageFailed(StorageError::Query);
        return 0;
    }
    ListeningStateJob job;
    const auto id = job.id = ++m_nextId;
    job.operation = ListeningStateOperation::LoadStats;
    job.accountId = m_source.accountId;
    job.stats = query;
    m_pending.insert(id);
    m_historyReads.insert(id, {m_source, m_historyEpoch, true});
    emit requested(std::move(job));
    return id;
}
void ListeningStateService::scheduleSongs(int delayMs)
{
    // A pending start is never brought forward: start-up writes do not cut its wait short.
    if (m_closed || m_songRead || m_songTicket || m_songSave || m_songTimer.isActive())
        return;
    m_songTimer.start(delayMs);
}
void ListeningStateService::requestMissingSongs()
{
    if (m_closed || !live() || m_songRead || m_songTicket || m_songSave)
        return;
    if (m_pending.size() >= 6 || m_historyClears.contains(m_source.accountId) ||
        m_clearRequests.values().contains(m_source.accountId)) {
        scheduleSongs(5000);
        return;
    }
    if (m_nextId == std::numeric_limits<quint64>::max())
        return;
    m_songSource = m_source;
    m_songEpoch = m_historyEpoch;
    ListeningStateJob job;
    m_songRead = job.id = ++m_nextId;
    job.operation = ListeningStateOperation::MissingSongs;
    job.accountId = m_source.accountId;
    m_pending.insert(job.id);
    emit requested(std::move(job));
}
void ListeningStateService::resolveSongs(quint64 ticket, const QList<domain::Song>& songs,
                                         const QList<domain::SongId>& unavailable, bool ok)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!ticket || ticket != m_songTicket)
        return;
    m_songTicket = 0;
    if (!ok) {
        log::info(lcStorage, "listening.songs_lookup_failed", {"count", m_songsAsked.size()});
        m_songsAsked.clear();
        scheduleSongs(10 * 60 * 1000); // not again right away; new records also try
        return;
    }
    if (m_closed || !live() || m_songSource != m_source || m_songEpoch != m_historyEpoch)
        return;
    // Only what was asked for, so a confused answer cannot add details to other songs.
    const auto asked = [this](const domain::SongId& id) { return m_songsAsked.contains(id); };
    ListeningStateJob job;
    job.operation = ListeningStateOperation::SaveSongs;
    job.accountId = m_source.accountId;
    for (const auto& song : songs)
        if (asked(song.id) && job.songs.size() < 100)
            job.songs.append(song);
    for (const auto& id : unavailable)
        if (asked(id) && job.unavailable.size() < 100)
            job.unavailable.append(id);
    if (job.songs.isEmpty() && job.unavailable.isEmpty())
        return;
    if (m_pending.size() >= 7 || m_nextId == std::numeric_limits<quint64>::max()) {
        m_songsAsked.clear(); // ask again later
        scheduleSongs(5000);
        return;
    }
    log::debug(lcStorage, "listening.songs_resolved",
               {{"found", job.songs.size()}, {"unavailable", job.unavailable.size()}});
    m_songSave = job.id = ++m_nextId;
    m_pending.insert(job.id);
    emit requested(std::move(job));
}
QList<ListeningStateJob> ListeningStateService::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed)
        return {};
    m_closed = true;
    m_songTimer.stop();
    m_songTicket = 0;
    m_loadId = 0;
    ++m_historyEpoch;
    m_timer.stop();
    m_historyTimer.stop();
    QList<ListeningStateJob> jobs;
    if (m_save && m_nextId != std::numeric_limits<quint64>::max()) {
        m_save->id = ++m_nextId;
        m_pending.insert(m_save->id);
        jobs.append(std::move(*m_save));
        m_save.reset();
    }
    jobs.append(historyJobs(264)); // <=256 facts/8 scope erasures, never a retention policy
    if (m_save || !m_historyDirty.isEmpty() || !m_historyClears.isEmpty())
        m_writeRejected = true;
    return jobs; // worker admits final jobs as earlier DB slots complete; no owner callback needed
}
} // namespace cirrus::storage
