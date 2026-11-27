#pragma once
#include "domain/playback/listening_state_types.h"

namespace cirrus::application
{
// Caller-thread value model. No disk/network, no accounting by observer arrival time.
class ListeningHistory
{
  public:
    static constexpr qsizetype MaximumRecords = 256;
    void begin(domain::LoadToken token, QString accountId, domain::SongId song,
               domain::ListeningMode source, QUuid queueEntry, domain::PlayOrigin origin = {},
               domain::ProgramId program = {}, QString alg = {});
    bool observe(const domain::PlaybackSnapshot& snapshot);
    // The playing load's queue entry got a 心动模式 mark (reported with it).
    void setAlg(QUuid queueEntry, const QString& alg);
    bool finish(domain::ListeningEnd end);
    void clear(); // forget rows and suppress this load; queued old totals cannot resurrect it
    void reset(); // account/lifetime change: forget everything
    [[nodiscard]] domain::SessionToken context() const { return m_context; }
    [[nodiscard]] QList<domain::ListeningRecord> records() const { return m_records; }
    // Single owner consumes each changed/finalized fact before the 256-row view can evict it.
    struct Update {
        domain::StoredListeningRecord record;
        domain::SourceProvider provider = domain::SourceProvider::Netease;
    };
    QList<Update> takeUpdates();
    bool takeUpdateOverflow();

  private:
    void recordUpdate(const domain::ListeningRecord& record);
    QList<Update> m_updates; // bounded transient facts, not DB retention
    bool m_updateOverflow = false;
    domain::SessionToken m_context;
    std::optional<domain::ListeningRecord> m_active;
    QList<domain::ListeningRecord> m_records; // newest first, only positive observed loads
    quint64 m_generation = 0;
    qint64 m_lastTotal = 0;
};
} // namespace cirrus::application
