#pragma once
#include "domain/playback/listening_state_types.h"
#include <functional>

namespace cirrus::application
{
// Copyable candidate state: caller commits only after a playback command is accepted.
class PlaybackQueue
{
  public:
    using RandomIndex = std::function<quint32(quint32)>;
    explicit PlaybackQueue(RandomIndex random = {});
    // Pure value restoration; no player, load token or historical traversal is restored.
    bool restore(const domain::QueueSnapshot& saved);
    // Where each song was picked from: one per song, or none. Appended songs share one.
    // `programs`: one per song or none; a valid one makes the entry that podcast program,
    // played through the song (its track).
    bool replace(const QList<domain::SongId>& songs, const QList<domain::PlayOrigin>& origins = {},
                 const QList<domain::ProgramId>& programs = {});
    bool append(const QList<domain::SongId>& songs, bool playNext = false,
                const domain::PlayOrigin& origin = {},
                const QList<domain::ProgramId>& programs = {});
    // 心动模式 (docs/tasks/heartbeat-mode.md §7.3): the queue becomes `mix` (songs in play
    // order with their origin and alg; ids are given here), in order and repeated, and the
    // queue before is kept for leaving the mode (already in it: the one kept then).
    // `keepActive`: the active entry stays first, then the songs put after it with playNext
    // since it became active, then the mix without the active song; else the mix alone,
    // nothing active. Past MaximumEntries the mix is cut. `activeAlg`: the mark the active
    // entry gets in the mix, as the official app gives the song playing (heartbeat-mode.md §8).
    bool enterHeartbeat(const domain::PlaylistId& playlist, const QList<domain::QueueEntry>& mix,
                        bool keepActive, const QString& activeAlg = {});
    [[nodiscard]] bool inHeartbeat() const { return m_state.heartbeat.has_value(); }
    bool select(QUuid id);
    bool next(bool naturalEnd = false, bool ignoreSingleRepeat = false);
    bool previous();
    [[nodiscard]] bool canNext() const;
    [[nodiscard]] bool canPrevious() const { return m_historyCursor > 0; }
    bool remove(QUuid id);
    // Puts the entry at place `to` of the play order (where it ends up).
    bool move(QUuid id, qsizetype to);
    void clear();
    // In 心动模式 this leaves it (§7.5): the queue before comes back with these modes, the
    // active entry still active (put after the one active back then when it was not in it).
    bool setModes(domain::QueueOrder order, domain::QueueRepeat repeat);
    [[nodiscard]] std::optional<domain::QueueEntry> current() const;
    [[nodiscard]] QUuid successor() const;
    [[nodiscard]] domain::QueueSnapshot snapshot() const { return m_state; }
    // Entry ids in play order (the shuffled order while shuffling); snapshot() keeps
    // insertion order.
    [[nodiscard]] QList<QUuid> playOrder() const { return m_order; }
    static constexpr qsizetype MaximumEntries = domain::MaximumSavedQueueEntries;

  private:
    void reshuffle();
    // m_order from the entries, shuffled when the order is (the active entry first).
    void reorder();
    void leaveHeartbeat(domain::QueueOrder order, domain::QueueRepeat repeat);
    bool choose(QUuid id, bool record);
    domain::QueueSnapshot m_state;
    QList<QUuid> m_order, m_history;
    qsizetype m_historyCursor = -1;
    QUuid m_insertTail;
    QUuid m_beforeInsertTail; // the heartbeat's queue before: where playNext goes in it
    RandomIndex m_random;
};
} // namespace cirrus::application
