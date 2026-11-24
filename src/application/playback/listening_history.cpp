#include "listening_history.h"
#include <QDateTime>
#include <algorithm>
#include <utility>

namespace cirrus::application
{
using namespace domain;
void ListeningHistory::begin(LoadToken token, QString accountId, SongId song, ListeningMode source,
                             QUuid queueEntry, PlayOrigin origin, ProgramId program)
{
    if (!validQueueProgram(program) || source == ListeningMode::PersonalFm)
        program = {};
    if (!validPlayOrigin(origin) ||
        (source == ListeningMode::PersonalFm && origin.kind != PlayOriginKind::None))
        origin = {}; // a bad origin never costs the record
    if (!token.session.isValid() || !token.generation || !validMusicId(song.value) ||
        (m_context == token.session && token.generation <= m_generation))
        return;
    if (m_context != token.session)
        reset();
    finish(ListeningEnd::Replaced);
    m_context = token.session;
    m_generation = token.generation;
    m_lastTotal = 0;
    m_active = ListeningRecord{};
    m_active->token = token;
    m_active->accountId = std::move(accountId);
    m_active->song = std::move(song);
    m_active->source = source;
    m_active->queueEntry = queueEntry;
    m_active->origin = std::move(origin);
    m_active->program = std::move(program);
    m_active->recordId = QUuid::createUuid();
    m_active->startedAtMs = QDateTime::currentMSecsSinceEpoch();
}
bool ListeningHistory::observe(const PlaybackSnapshot& snapshot)
{
    if (!m_active || snapshot.token != m_active->token || snapshot.song != m_active->song)
        return false;
    bool changed = false;
    if (m_active->playedMs == 0)
        m_active->provider = snapshot.provider;
    if (snapshot.observed) {
        const auto value = *snapshot.observed;
        if (value.playedMs >= m_lastTotal && value.playedMs <= ObservedPlayback::MaximumMs &&
            (!value.capped || value.playedMs == ObservedPlayback::MaximumMs) &&
            (!m_active->capped || value.capped)) {
            m_lastTotal = value.playedMs;
            const auto played = value.playedMs;
            if (played > m_active->playedMs || value.capped != m_active->capped) {
                m_active->playedMs = played;
                m_active->capped = value.capped;
                m_active->preview = snapshot.preview;
                m_active->actualQuality = snapshot.actualQuality;
                if (played > 0) {
                    if (!m_records.isEmpty() && m_records.first().token == snapshot.token)
                        m_records.first() = *m_active;
                    else {
                        m_records.prepend(*m_active);
                        if (m_records.size() > MaximumRecords)
                            m_records.removeLast();
                    }
                    recordUpdate(*m_active);
                    changed = true;
                }
            }
        }
    }
    if (snapshot.state == PlaybackState::Ended)
        changed = finish(ListeningEnd::Ended) || changed;
    else if (snapshot.state == PlaybackState::Failed)
        changed = finish(ListeningEnd::Failed) || changed;
    return changed;
}
bool ListeningHistory::finish(ListeningEnd end)
{
    if (!m_active)
        return false;
    const bool recorded = !m_records.isEmpty() && m_records.first().token == m_active->token;
    m_active->end = end;
    if (end != ListeningEnd::Active)
        m_active->endedAtMs = std::max(m_active->startedAtMs, QDateTime::currentMSecsSinceEpoch());
    if (recorded)
        m_records.first() = *m_active;
    if (m_active->playedMs > 0)
        recordUpdate(*m_active);
    m_active.reset();
    return recorded;
}
void ListeningHistory::clear()
{
    m_updates.clear(); // explicit erasure supersedes any fact not yet handed to persistence
    m_records.clear();
    m_active.reset();
    m_lastTotal = 0;
}
void ListeningHistory::reset()
{
    finish(ListeningEnd::Detached); // retain the original account's final positive fact
    m_records.clear();
    m_active.reset();
    m_context = {};
    m_generation = 0;
    m_lastTotal = 0;
}
void ListeningHistory::recordUpdate(const ListeningRecord& record)
{
    auto row = storedListeningRecord(record);
    for (auto& pending : m_updates)
        if (pending.record.id == row.id) {
            pending = {std::move(row), record.provider};
            return;
        }
    if (m_updates.size() >= 64) {
        m_updateOverflow = true; // explicit storage gap, never silently truncate durable history
        return;
    }
    m_updates.append({std::move(row), record.provider});
}
QList<ListeningHistory::Update> ListeningHistory::takeUpdates()
{
    return std::exchange(m_updates, {});
}
bool ListeningHistory::takeUpdateOverflow() { return std::exchange(m_updateOverflow, false); }
} // namespace cirrus::application
