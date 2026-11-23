#pragma once
#include "application/backend/backend_facade.h"
#include "domain/playback/listening_report.h"
#include "domain/playback/listening_state_types.h"
#include <QDateTime>
#include <QSet>
#include <QTimer>
#include <functional>
#include <optional>

namespace cirrus::application
{
class ListeningSession;

// Reports what was listened to (docs/ui/integration.md §6.1, D5), as the web client
// does: a StartPlay when a load is first heard, then one Play with the genuinely
// observed seconds once it is settled: played to the end, replaced, stopped, failed, or
// paused for longer than the pause timeout. Plays under 3 s are not reported; guests
// and personal FM (no evidence for its source) get neither. The source
// comes from the caller per queue entry; without one the song itself is the source.
// Results are not retried (the facade doesn't either). Like the web player, it also logs
// the client as active (ActiveClient): once the account is signed in, and again before a
// report once a day has begun (at 6:00 local time) or when the last one wasn't
// acknowledged (docs/api/explicit-listening-reports.md §4c). Caller-thread object; facade
// and session must outlive it.
class ListeningReporter final : public QObject
{
    Q_OBJECT
  public:
    struct Origin {
        domain::ListeningReportSource source = domain::ListeningReportSource::List;
        QString id;
    };
    using OriginLookup = std::function<std::optional<Origin>(QUuid queueEntry)>;
    static constexpr qint64 MinimumMs = 3000;
    static constexpr int DefaultPauseTimeoutMs = 5 * 60 * 1000;

    ListeningReporter(BackendFacade& facade, ListeningSession& session, QObject* parent = nullptr);
    ~ListeningReporter() override;

    void setOriginLookup(OriginLookup lookup) { m_lookup = std::move(lookup); }
    void setPauseTimeout(int ms) { m_pause.setInterval(ms); }
    // The local time (tests); the day for ActiveClient begins at 6:00 of it.
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }
    static QDateTime dayStart(const QDateTime& now);
    // Closing: settles the load playing now, then calls `done` once every report in
    // flight has answered, or after `timeoutMs`. `done` runs once, later, never inside.
    void flush(std::function<void()> done, int timeoutMs = 2000);

  private:
    void recordChanged(const domain::StoredListeningRecord& row, domain::SourceProvider provider);
    void playbackChanged(const domain::PlaybackSnapshot& snapshot);
    void report(const domain::StoredListeningRecord& row, domain::ListeningReportEnd end);
    struct Prepared {
        domain::ListeningReport report;
        const char* origin; // the source: "known", "invalid" (so the song) or "none"
    };
    // The report for `row`, or none when it must not be sent (logged).
    std::optional<Prepared> prepare(const domain::StoredListeningRecord& row,
                                    domain::ListeningReportAction action);
    void send(const Prepared& prepared, const domain::StoredListeningRecord& row);
    static void remember(QSet<QUuid>& set, QList<QUuid>& order, QUuid id);
    void finishFlush();
    void markActive(); // sends ActiveClient unless this account's day already has one

    BackendFacade& m_facade;
    ListeningSession& m_session;
    OriginLookup m_lookup;
    std::optional<domain::StoredListeningRecord> m_active; // the load playing, its latest total
    QSet<QUuid> m_reported;                                // record ids already settled
    QList<QUuid> m_reportedOrder;                          // bounds m_reported
    QSet<QUuid> m_started;                                 // record ids StartPlay was sent for
    QList<QUuid> m_startedOrder;                           // bounds m_started
    QSet<quint64> m_inFlight;
    QTimer m_pause;
    QTimer m_flushTimer;
    std::function<void()> m_flushDone;
    std::function<QDateTime()> m_clock;
    QString m_activeAccount;            // the account the last ActiveClient was for
    QDateTime m_activeAt;               // when it was sent; null: none, or not acknowledged
    quint64 m_activeId = 0;             // the ActiveClient in flight
    QHash<QString, qint64> m_durations; // song id → its length in seconds, as played
};
} // namespace cirrus::application
