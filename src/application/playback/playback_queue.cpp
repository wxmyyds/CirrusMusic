#include "playback_queue.h"
#include <QHash>
#include <QRandomGenerator>
#include <algorithm>
#include <utility>

namespace cirrus::application
{
using namespace domain;
PlaybackQueue::PlaybackQueue(RandomIndex random) : m_random(std::move(random))
{
    if (!m_random)
        m_random = [](quint32 bound) { return QRandomGenerator::global()->bounded(bound); };
}
void PlaybackQueue::clear()
{
    // An emptied or replaced queue ends 心动模式: back to the modes from before it.
    if (const auto before = std::exchange(m_state.heartbeat, std::nullopt)) {
        m_state.order = before->order;
        m_state.repeat = before->repeat;
    }
    m_beforeInsertTail = {};
    m_state.entries.clear();
    m_state.active = {};
    m_order.clear();
    m_history.clear();
    m_historyCursor = -1;
    m_insertTail = {};
}
bool PlaybackQueue::restore(const QueueSnapshot& saved)
{
    if (!validSavedQueue(saved))
        return false;
    clear();
    m_state = saved;
    reorder();
    if (!saved.active.isNull()) {
        m_history.append(saved.active);
        m_historyCursor = 0;
    }
    return true;
}
bool PlaybackQueue::replace(const QList<SongId>& songs, const QList<PlayOrigin>& origins,
                            const QList<ProgramId>& programs)
{
    if (songs.isEmpty() || songs.size() > MaximumEntries ||
        (!origins.isEmpty() && origins.size() != songs.size()) ||
        (!programs.isEmpty() && programs.size() != songs.size()))
        return false;
    for (const auto& program : programs)
        if (!validQueueProgram(program))
            return false;
    for (const auto& song : songs)
        if (!validMusicId(song.value))
            return false;
    for (const auto& origin : origins)
        if (!validPlayOrigin(origin))
            return false;
    clear();
    if (!append(songs, false, {}, programs))
        return false;
    for (qsizetype i = 0; i < origins.size(); ++i)
        m_state.entries[i].origin = origins.at(i);
    return true;
}
bool PlaybackQueue::append(const QList<SongId>& songs, bool playNext, const PlayOrigin& origin,
                           const QList<ProgramId>& programs)
{
    if (songs.isEmpty() || songs.size() > MaximumEntries - m_state.entries.size() ||
        !validPlayOrigin(origin) || (!programs.isEmpty() && programs.size() != songs.size()))
        return false;
    for (const auto& song : songs)
        if (!validMusicId(song.value))
            return false;
    for (const auto& program : programs)
        if (!validQueueProgram(program))
            return false;
    QList<QueueEntry> entries;
    QList<QUuid> ids;
    for (qsizetype i = 0; i < songs.size(); ++i) {
        const auto id = QUuid::createUuid();
        entries.append(
            {id, songs.at(i), origin, programs.isEmpty() ? ProgramId{} : programs.at(i)});
        ids.append(id);
    }
    if (playNext && !m_state.active.isNull()) {
        // An explicit next-play insertion supersedes the old forward navigation branch.
        while (m_history.size() > m_historyCursor + 1)
            m_history.removeLast();
        const auto tail = m_insertTail.isNull() ? m_state.active : m_insertTail;
        const auto i = m_order.indexOf(tail);
        const auto it = std::find_if(m_state.entries.begin(), m_state.entries.end(),
                                     [tail](const auto& e) { return e.id == tail; });
        const auto displayIndex = it - m_state.entries.begin();
        for (qsizetype n = 0; n < entries.size(); ++n) {
            m_order.insert(i + 1 + n, ids[n]);
            m_state.entries.insert(displayIndex + 1 + n, entries[n]);
        }
        m_insertTail = ids.last();
    } else {
        m_state.entries.append(entries);
        m_order.append(ids);
        if (m_state.order == QueueOrder::Shuffle && m_state.active.isNull())
            reshuffle();
    }
    // In 心动模式 the queue before gets them too (same ids), so they are there after it
    // (§7.4): next ones after the song playing back then, the others at its end.
    if (m_state.heartbeat && m_state.heartbeat->entries.size() <= MaximumEntries - entries.size()) {
        auto& before = m_state.heartbeat->entries;
        const auto tail =
            m_beforeInsertTail.isNull() ? m_state.heartbeat->active : m_beforeInsertTail;
        const auto at = std::find_if(before.cbegin(), before.cend(),
                                     [tail](const auto& e) { return e.id == tail; });
        if (playNext && !tail.isNull() && at != before.cend()) {
            const auto index = at - before.cbegin();
            for (qsizetype n = 0; n < entries.size(); ++n)
                before.insert(index + 1 + n, entries.at(n));
            m_beforeInsertTail = ids.last();
        } else {
            before.append(entries);
        }
    }
    return true;
}
bool PlaybackQueue::enterHeartbeat(const PlaylistId& playlist, const QList<QueueEntry>& mix,
                                   bool keepActive, const QString& activeAlg)
{
    if (mix.isEmpty() || !validMusicId(playlist.value) || !validQueueAlg(activeAlg))
        return false;
    for (const auto& entry : mix)
        if (!validMusicId(entry.song.value) || !validPlayOrigin(entry.origin) ||
            !entry.program.value.isEmpty() || !validQueueAlg(entry.alg))
            return false;
    QList<QueueEntry> entries;
    const auto active = current();
    bool keptNext = false;
    if (keepActive) {
        if (!active)
            return false;
        entries.append(*active);
        if (!activeAlg.isEmpty())
            entries.first().alg = activeAlg; // in the mix only: the queue before keeps its own
        // The songs put next since it became active: up to m_insertTail in play order.
        if (const auto tail = m_order.indexOf(m_insertTail); tail >= 0) {
            keptNext = true;
            QHash<QUuid, QueueEntry> byId;
            for (const auto& entry : std::as_const(m_state.entries))
                byId.insert(entry.id, entry);
            for (auto i = m_order.indexOf(active->id) + 1; i >= 1 && i <= tail; ++i)
                entries.append(byId.value(m_order.at(i)));
        }
    }
    for (const auto& entry : mix) {
        if (entries.size() == MaximumEntries)
            break;
        if (keepActive && entry.song == active->song)
            continue;
        entries.append({QUuid::createUuid(), entry.song, entry.origin, {}, entry.alg});
    }
    if (!m_state.heartbeat)
        m_state.heartbeat = HeartbeatState{playlist, m_state.entries, m_state.active, m_state.order,
                                           m_state.repeat};
    else
        m_state.heartbeat->playlist = playlist;
    m_beforeInsertTail = {};
    m_state.entries = std::move(entries);
    m_state.order = QueueOrder::Sequential;
    m_state.repeat = QueueRepeat::All;
    m_history.clear();
    m_historyCursor = -1;
    if (keepActive) {
        m_history.append(active->id);
        m_historyCursor = 0;
    } else {
        m_state.active = {};
    }
    if (!keptNext)
        m_insertTail = {}; // else still the last of them: more go after
    reorder();
    return true;
}
void PlaybackQueue::leaveHeartbeat(QueueOrder order, QueueRepeat repeat)
{
    auto before = std::move(*m_state.heartbeat);
    m_state.heartbeat.reset();
    m_beforeInsertTail = {};
    const auto active = current();
    m_state.entries = std::move(before.entries);
    m_state.active = {};
    if (active) {
        // Still playing: its entry in the queue before, else put after the one active then.
        const auto in = [&](QUuid id) {
            return std::find_if(m_state.entries.cbegin(), m_state.entries.cend(),
                                [id](const auto& e) { return e.id == id; });
        };
        if (in(active->id) == m_state.entries.cend()) {
            const auto then = in(before.active);
            const auto index = then == m_state.entries.cend() ? m_state.entries.size()
                                                              : then - m_state.entries.cbegin() + 1;
            if (m_state.entries.size() == MaximumEntries)
                m_state.entries.removeLast();
            m_state.entries.insert(std::min(index, m_state.entries.size()), *active);
        }
        m_state.active = active->id;
    }
    m_state.order = order;
    m_state.repeat = repeat;
    m_history.clear();
    m_historyCursor = -1;
    if (!m_state.active.isNull()) {
        m_history.append(m_state.active);
        m_historyCursor = 0;
    }
    m_insertTail = {};
    reorder();
}
void PlaybackQueue::reorder()
{
    m_order.clear();
    for (const auto& entry : std::as_const(m_state.entries))
        m_order.append(entry.id);
    if (m_state.order == QueueOrder::Shuffle) {
        m_order.removeAll(m_state.active);
        reshuffle();
        if (!m_state.active.isNull())
            m_order.prepend(m_state.active);
    }
}
void PlaybackQueue::reshuffle()
{
    for (qsizetype i = m_order.size() - 1; i > 0; --i) {
        const auto j = m_random(quint32(i + 1)) % quint32(i + 1);
        m_order.swapItemsAt(i, j);
    }
}
bool PlaybackQueue::setModes(QueueOrder order, QueueRepeat repeat)
{
    if ((order != QueueOrder::Sequential && order != QueueOrder::Shuffle) ||
        (repeat != QueueRepeat::Off && repeat != QueueRepeat::All && repeat != QueueRepeat::One))
        return false;
    if (m_state.heartbeat) {
        leaveHeartbeat(order, repeat);
        return true;
    }
    if (order != m_state.order) {
        while (m_history.size() > m_historyCursor + 1)
            m_history.removeLast();
        m_state.order = order;
        reorder();
        m_insertTail = {};
    }
    m_state.order = order;
    m_state.repeat = repeat;
    return true;
}
bool PlaybackQueue::choose(QUuid id, bool record)
{
    if (id.isNull() || !m_order.contains(id))
        return false;
    if (m_state.active.isNull() && m_state.order == QueueOrder::Shuffle) {
        m_order.removeAll(id);
        m_order.prepend(
            id); // first explicit selection must not discard the front of a shuffle round
    }
    if (record) {
        while (m_history.size() > m_historyCursor + 1)
            m_history.removeLast();
        if (m_history.isEmpty() || m_history.last() != id)
            m_history.append(id);
        if (m_history.size() > 1024)
            m_history.removeFirst();
        m_historyCursor = m_history.size() - 1;
    }
    if (m_state.active != id)
        m_insertTail = {};
    m_state.active = id;
    return true;
}
bool PlaybackQueue::select(QUuid id) { return choose(id, true); }
bool PlaybackQueue::next(bool naturalEnd, bool ignoreSingleRepeat)
{
    if (m_order.isEmpty())
        return false;
    if (naturalEnd && !ignoreSingleRepeat && m_state.repeat == QueueRepeat::One &&
        !m_state.active.isNull())
        return true;
    if (m_historyCursor + 1 < m_history.size())
        return choose(m_history[++m_historyCursor], false);
    const auto index = m_order.indexOf(m_state.active);
    if (index + 1 < m_order.size())
        return choose(m_order[index + 1], true);
    if (m_state.repeat == QueueRepeat::Off)
        return false;
    if (m_state.order == QueueOrder::Shuffle) {
        reshuffle();
        if (m_order.size() > 1 && m_order.first() == m_state.active)
            m_order.swapItemsAt(0, 1);
    }
    return choose(m_order.first(), true);
}
bool PlaybackQueue::canNext() const
{
    return !m_order.isEmpty() && (m_historyCursor + 1 < m_history.size() ||
                                  m_order.indexOf(m_state.active) + 1 < m_order.size() ||
                                  m_state.repeat != QueueRepeat::Off);
}
bool PlaybackQueue::previous()
{
    return m_historyCursor > 0 && choose(m_history[--m_historyCursor], false);
}
QUuid PlaybackQueue::successor() const
{
    const auto i = m_order.indexOf(m_state.active);
    return i >= 0 && i + 1 < m_order.size() ? m_order[i + 1] : QUuid{};
}
bool PlaybackQueue::remove(QUuid id)
{
    if (!m_order.contains(id))
        return false;
    m_order.removeAll(id);
    m_state.entries.removeIf([id](const auto& entry) { return entry.id == id; });
    for (qsizetype i = m_history.size(); i-- > 0;)
        if (m_history[i] == id) {
            m_history.removeAt(i);
            if (i <= m_historyCursor)
                --m_historyCursor;
        }
    if (m_state.active == id)
        m_state.active = {};
    if (m_insertTail == id)
        m_insertTail = {};
    return true;
}
bool PlaybackQueue::move(QUuid id, qsizetype to)
{
    const auto from = m_order.indexOf(id);
    if (from < 0 || to < 0 || to >= m_order.size())
        return false;
    m_order.move(from, to);
    if (m_state.order == QueueOrder::Sequential) {
        // In order the entries are the play order: keep them so, or switching shuffle on and
        // off would undo the move.
        QHash<QUuid, QueueEntry> byId;
        for (const auto& entry : std::as_const(m_state.entries))
            byId.insert(entry.id, entry);
        QList<QueueEntry> entries;
        entries.reserve(m_order.size());
        for (const auto& entryId : std::as_const(m_order))
            entries.append(byId.value(entryId));
        m_state.entries = std::move(entries);
    }
    m_insertTail = {};
    return true;
}
std::optional<QueueEntry> PlaybackQueue::current() const
{
    for (const auto& entry : m_state.entries)
        if (entry.id == m_state.active)
            return entry;
    return std::nullopt;
}
} // namespace cirrus::application
