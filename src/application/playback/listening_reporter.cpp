#include "listening_reporter.h"
#include "listening_session.h"
#include "logging/log.h"
#include <QThread>
#include <QTimeZone>

namespace cirrus::application
{
using namespace domain;
namespace
{
constexpr qsizetype kRememberedRecords = 512;

ListeningReportEnd endOf(ListeningEnd end)
{
    switch (end) {
    case ListeningEnd::Ended:
        return ListeningReportEnd::PlayEnd;
    case ListeningEnd::Replaced:
        return ListeningReportEnd::Ui; // the user went on (an automatic advance ends as Ended)
    case ListeningEnd::Failed:
        return ListeningReportEnd::Exception;
    default:
        return ListeningReportEnd::Interrupt;
    }
}
} // namespace

ListeningReporter::ListeningReporter(BackendFacade& facade, ListeningSession& session,
                                     QObject* parent)
    : QObject(parent), m_facade(facade), m_session(session)
{
    Q_ASSERT(facade.thread() == thread() && session.thread() == thread());
    m_pause.setSingleShot(true);
    m_pause.setInterval(DefaultPauseTimeoutMs);
    connect(&m_pause, &QTimer::timeout, this, [this] {
        if (m_active && m_session.playback().state == PlaybackState::Paused)
            report(*m_active, ListeningReportEnd::Interrupt);
    });
    m_flushTimer.setSingleShot(true);
    connect(&m_flushTimer, &QTimer::timeout, this, &ListeningReporter::finishFlush);
    connect(&session, &ListeningSession::reportingRecordChanged, this,
            &ListeningReporter::recordChanged);
    connect(&session, &ListeningSession::playbackChanged, this,
            &ListeningReporter::playbackChanged);
    connect(&facade, &BackendFacade::listeningReportCompleted, this,
            [this](quint64 id, SessionToken, const ListeningReportResult& result) {
                if (id == m_activeId) {
                    m_activeId = 0;
                    if (result.outcome != ReportOutcome::Acknowledged)
                        m_activeAt = {}; // try again before the next report
                }
                if (m_inFlight.remove(id) && m_inFlight.isEmpty() && m_flushDone)
                    finishFlush();
            });
    connect(&facade, &BackendFacade::sessionChanged, this, &ListeningReporter::markActive);
    connect(&facade, &BackendFacade::stateChanged, this, &ListeningReporter::markActive);
    markActive();
}

ListeningReporter::~ListeningReporter() = default;

void ListeningReporter::recordChanged(const StoredListeningRecord& row, SourceProvider provider)
{
    if (provider == SourceProvider::Bilibili) {
        remember(m_started, m_startedOrder, row.id);
        remember(m_reported, m_reportedOrder, row.id);
        if (m_active && m_active->id == row.id)
            m_active.reset();
        return;
    }
    if (row.end == ListeningEnd::Active) {
        m_active = row;
        if (!m_started.contains(row.id)) { // first heard: the web's startplay
            remember(m_started, m_startedOrder, row.id);
            if (auto start = prepare(row, ListeningReportAction::StartPlay))
                send(*start, row);
        }
        return;
    }
    if (m_active && m_active->id == row.id)
        m_active.reset();
    report(row, endOf(row.end));
}

void ListeningReporter::playbackChanged(const PlaybackSnapshot& snapshot)
{
    if (snapshot.durationMs && *snapshot.durationMs > 0 && validMusicId(snapshot.song.value)) {
        if (m_durations.size() > 64)
            m_durations.clear();
        m_durations.insert(snapshot.song.value, qMin<qint64>(*snapshot.durationMs / 1000, 86400));
    }
    if (snapshot.state == PlaybackState::Paused) {
        if (!m_pause.isActive())
            m_pause.start();
    } else
        m_pause.stop();
}

void ListeningReporter::remember(QSet<QUuid>& set, QList<QUuid>& order, QUuid id)
{
    set.insert(id);
    order.append(id);
    if (order.size() > kRememberedRecords)
        set.remove(order.takeFirst());
}

std::optional<ListeningReporter::Prepared>
ListeningReporter::prepare(const StoredListeningRecord& row, ListeningReportAction action)
{
    const auto session = m_facade.snapshot();
    if (row.source == ListeningMode::PersonalFm || row.accountId.isEmpty() ||
        session.phase != SessionPhase::Authenticated || session.accountId != row.accountId) {
        log::info(lcReport, "report.skip",
                  {{"action", action},
                   {"reason", row.source == ListeningMode::PersonalFm        ? "fm"
                              : row.accountId.isEmpty()                      ? "guest"
                              : session.phase != SessionPhase::Authenticated ? "signed_out"
                                                                             : "other_account"},
                   {"song", row.song.value}});
        return std::nullopt; // FM: no evidence for its source; guests can't; never another
                             // account's
    }
    ListeningReport report{QUuid::createUuid(), row.song, row.song.value, action,
                           action == ListeningReportAction::Play ? row.playedMs / 1000 : 0};
    report.source = ListeningReportSource::Song; // played as itself, unless we know better
    report.quality = row.actualQuality.size() <= 16 ? row.actualQuality : QString();
    report.durationSeconds = m_durations.value(row.song.value);
    report.startedAtMs = qMax<qint64>(0, row.startedAtMs);
    const char* origin = "none";
    if (const auto found = m_lookup ? m_lookup(row.queueEntry) : std::nullopt) {
        auto known = report;
        known.source = found->source;
        known.sourceId = found->id;
        origin = known.isValid() ? "known" : "invalid";
        if (known.isValid())
            report = known;
    }
    return Prepared{report, origin};
}

QDateTime ListeningReporter::dayStart(const QDateTime& now)
{
    QDateTime start(now.date(), QTime(6, 0), now.timeZone());
    return now < start ? start.addDays(-1) : start;
}

void ListeningReporter::markActive()
{
    const auto session = m_facade.snapshot();
    if (session.phase != SessionPhase::Authenticated || m_activeId)
        return;
    const auto now = m_clock ? m_clock() : QDateTime::currentDateTime();
    if (session.accountId == m_activeAccount && m_activeAt.isValid() && m_activeAt >= dayStart(now))
        return;
    ListeningReport report;
    report.eventId = QUuid::createUuid();
    report.action = ListeningReportAction::ActiveClient;
    const auto id = m_facade.reportListening(report);
    log::debug(lcReport, "report.active", {{"event", report.eventId}, {"queued", id != 0}});
    if (!id)
        return; // not ready yet: the next session change or report tries again
    m_activeId = id;
    m_activeAccount = session.accountId;
    m_activeAt = now;
    m_inFlight.insert(id);
}

void ListeningReporter::send(const Prepared& prepared, const StoredListeningRecord& row)
{
    markActive(); // a new day (6:00) or a failed one: the web player's activeclient first
    const auto& report = prepared.report;
    log::debug(lcReport, "report.settled",
               {{"event", report.eventId},
                {"action", report.action},
                {"song", row.song.value},
                {"record_end", row.end},
                {"end", report.end},
                {"played_ms", row.playedMs},
                {"origin", prepared.origin},
                {"source", report.source}});
    if (const auto id = m_facade.reportListening(report))
        m_inFlight.insert(id);
    else
        log::warn(lcReport, "report.not_queued", {"event", report.eventId});
}

void ListeningReporter::report(const StoredListeningRecord& row, ListeningReportEnd end)
{
    if (row.id.isNull() || m_reported.contains(row.id))
        return;
    if (row.playedMs < MinimumMs) {
        log::debug(lcReport, "report.skip",
                   {{"reason", "short"}, {"song", row.song.value}, {"played_ms", row.playedMs}});
        return; // a short load may still grow while it plays
    }
    remember(m_reported, m_reportedOrder, row.id);
    if (auto play = prepare(row, ListeningReportAction::Play)) {
        play->report.end = end;
        send(*play, row);
    }
}

void ListeningReporter::flush(std::function<void()> done, int timeoutMs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_flushDone = std::move(done);
    m_session.finishHistoryForStorage(); // settles the playing load (recordChanged)
    log::info(lcReport, "report.flush", {"in_flight", m_inFlight.size()});
    if (m_inFlight.isEmpty()) {
        QTimer::singleShot(0, this, &ListeningReporter::finishFlush);
        return;
    }
    m_flushTimer.start(timeoutMs);
}

void ListeningReporter::finishFlush()
{
    if (m_flushDone && !m_inFlight.isEmpty())
        log::warn(lcReport, "report.flush_timeout", {"in_flight", m_inFlight.size()});
    m_flushTimer.stop();
    if (auto done = std::exchange(m_flushDone, {}))
        done();
}
} // namespace cirrus::application
