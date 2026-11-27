#pragma once

#include "listening_history.h"
#include "listening_types.h"

#include <QSet>

namespace cirrus::domain
{
inline constexpr qsizetype MaximumSavedQueueEntries = 10000;

namespace detail
{
inline bool validQueueList(const QList<QueueEntry>& entries, QUuid active, QueueOrder order,
                           QueueRepeat repeat)
{
    if (entries.size() > MaximumSavedQueueEntries || order < QueueOrder::Sequential ||
        order > QueueOrder::Shuffle || repeat < QueueRepeat::Off || repeat > QueueRepeat::One)
        return false;
    QSet<QUuid> ids;
    for (const auto& entry : entries) {
        if (entry.id.isNull() || ids.contains(entry.id) || !validMusicId(entry.song.value) ||
            !validPlayOrigin(entry.origin) || !validQueueProgram(entry.program) ||
            !validQueueAlg(entry.alg))
            return false;
        ids.insert(entry.id);
    }
    return active.isNull() || ids.contains(active);
}
} // namespace detail
inline bool validSavedQueue(const QueueSnapshot& queue)
{
    if (!detail::validQueueList(queue.entries, queue.active, queue.order, queue.repeat))
        return false;
    if (!queue.heartbeat)
        return true;
    // A heartbeat mix plays in its own order, round and round.
    const auto& before = *queue.heartbeat;
    return queue.order == QueueOrder::Sequential && queue.repeat == QueueRepeat::All &&
           validMusicId(before.playlist.value) &&
           detail::validQueueList(before.entries, before.active, before.order, before.repeat);
}

// Guest is a distinct installation-local bucket, never assigned to a logged-in account.
inline bool validListeningAccount(const QString& account)
{
    return account.isEmpty() || validMusicId(account);
}
// Durable facts have no SessionToken/LoadToken, URL, credentials or provider payload.
struct StoredListeningRecord {
    QUuid id;
    QString accountId;
    SongId song;
    ListeningMode source = ListeningMode::Queue;
    QUuid queueEntry;
    qint64 startedAtMs = 0;
    std::optional<qint64> endedAtMs;
    qint64 playedMs = 0;
    bool capped = false;
    ListeningEnd end = ListeningEnd::Active;
    std::optional<PreviewRange> preview;
    QString actualQuality;
    PlayOrigin origin{}; // fixed when the record is first stored
    ProgramId program{}; // a podcast program played through `song`, its track
    QString alg{};       // the queue entry's 心动模式 mark, for the reports; not stored
};
inline StoredListeningRecord storedListeningRecord(const ListeningRecord& row)
{
    return {row.recordId,    row.accountId,     row.song,     row.source,  row.queueEntry,
            row.startedAtMs, row.endedAtMs,     row.playedMs, row.capped,  row.end,
            row.preview,     row.actualQuality, row.origin,   row.program, row.alg};
}
inline bool validStoredListeningRecord(const StoredListeningRecord& row)
{
    return !row.id.isNull() && validListeningAccount(row.accountId) &&
           validMusicId(row.song.value) && row.source >= ListeningMode::Queue &&
           row.source <= ListeningMode::PersonalFm && row.startedAtMs > 0 &&
           row.startedAtMs <= 253402300799999LL &&
           (!row.endedAtMs ||
            (*row.endedAtMs >= row.startedAtMs && *row.endedAtMs <= 253402300799999LL)) &&
           row.playedMs > 0 && row.playedMs <= ObservedPlayback::MaximumMs &&
           (!row.capped || row.playedMs == ObservedPlayback::MaximumMs) &&
           row.end >= ListeningEnd::Active && row.end <= ListeningEnd::Detached &&
           ((row.end == ListeningEnd::Active) == !row.endedAtMs.has_value()) &&
           (!row.preview ||
            (row.preview->startMs >= 0 && row.preview->endMs > row.preview->startMs &&
             row.preview->endMs <= ObservedPlayback::MaximumMs)) &&
           row.actualQuality.size() <= 64 && row.actualQuality.isValidUtf16() &&
           !row.actualQuality.contains(QChar::Null) && validPlayOrigin(row.origin) &&
           validQueueProgram(row.program) &&
           (row.source == ListeningMode::Queue || row.origin.kind == PlayOriginKind::None);
}
struct ListeningHistoryCursor {
    qint64 startedAtMs = 0;
    QUuid id;
};
struct ListeningHistoryPage {
    QList<StoredListeningRecord> records;
    std::optional<ListeningHistoryCursor> next;
};
} // namespace cirrus::domain

Q_DECLARE_METATYPE(cirrus::domain::StoredListeningRecord)
Q_DECLARE_METATYPE(cirrus::domain::ListeningHistoryPage)
