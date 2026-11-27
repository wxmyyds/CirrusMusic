#include "listening_report_gateway.h"
#include "logging/log.h"
#include <QThread>
#include <QTimer>

namespace cirrus::netease
{
using namespace domain;
namespace
{
ListeningReportResult mapResult(const ApiResult& api)
{
    ListeningReportResult result;
    result.outcome = api.submittedToTransport ? ReportOutcome::Unknown : ReportOutcome::NotSent;
    if (api.error == ApiError::None && api.decoded.ok() && api.decoded.businessCode) {
        result.providerCode = api.decoded.businessCode;
        result.outcome = *api.decoded.businessCode == 200 ? ReportOutcome::Acknowledged
                                                          : ReportOutcome::Rejected;
        result.error =
            *api.decoded.businessCode == 200 ? ReportError::None : ReportError::ProviderRejected;
    } else if (api.error == ApiError::Canceled)
        result.error = ReportError::Canceled;
    else if (api.error == ApiError::StaleContext)
        result.error = ReportError::StaleContext;
    else if (api.error == ApiError::Busy)
        result.error = ReportError::Busy;
    else if (api.error == ApiError::InvalidRequest)
        result.error = ReportError::InvalidRequest;
    else if (api.error == ApiError::Cookie)
        result.error = ReportError::AuthenticationRequired;
    else if (api.error == ApiError::Decode || api.error == ApiError::None)
        result.error = ReportError::InvalidResponse;
    else if (api.transportError == TransportError::Timeout)
        result.error = ReportError::Timeout;
    else if (api.error == ApiError::Transport)
        result.error = ReportError::Network;
    else
        result.error = ReportError::ProtocolFailure;
    if (api.httpStatus == 429 || api.decoded.businessCode == 429) {
        result.error = ReportError::RateLimited;
        bool ok = false;
        const auto seconds = api.retryAfter.toLongLong(&ok);
        if (ok && seconds >= 0 && seconds <= 86400)
            result.retryAfterMs = seconds * 1000;
    }
    return result;
}
} // namespace
ListeningReportGateway::ListeningReportGateway(application::SessionCoordinator& session,
                                               SessionApiClient& client, QObject* parent)
    : IListeningReportGateway(parent), m_session(session), m_client(&client)
{
    Q_ASSERT(session.thread() == thread() && client.thread() == thread());
    connect(&client, &SessionApiClient::completed, this,
            [this](quint64 apiId, SessionToken token, const ApiResult& api) {
                const auto id = m_ids.value(apiId);
                if (!m_pending.contains(id))
                    return;
                auto result = mapResult(api);
                // The answer's shape (field names only, never values): what to look at
                // when the provider accepts a report but doesn't count it.
                if (api.error == ApiError::None)
                    log::debug(lcReport, "report.response",
                               {{"id", id}, {"keys", api.decoded.object.keys().join(u',')}});
                const auto& p = m_pending[id];
                if (p.context != token || !m_session.accepts(p.context) ||
                    m_session.snapshot().phase != SessionPhase::Authenticated ||
                    m_session.snapshot().accountId != p.accountId) {
                    result.outcome =
                        api.submittedToTransport ? ReportOutcome::Unknown : ReportOutcome::NotSent;
                    result.error = ReportError::StaleContext;
                    result.providerCode.reset();
                }
                finish(id, result);
            });
    connect(&session, &application::SessionCoordinator::changed, this, [this](SessionSnapshot s) {
        if (!m_client)
            return;
        const auto pending =
            m_pending; // cancel may synchronously emit a completion and remove rows
        for (const auto& p : pending)
            if (m_client && (p.context != s.token || s.phase != SessionPhase::Authenticated ||
                             p.accountId != s.accountId))
                m_client->cancel(p.apiId);
    });
    connect(&client, &QObject::destroyed, this, [this] {
        for (auto id : m_pending.keys()) {
            ListeningReportResult result;
            result.outcome = m_pending[id].apiId ? ReportOutcome::Unknown : ReportOutcome::NotSent;
            result.error = ReportError::Network;
            finish(id, result);
        }
    });
}
ListeningReportGateway::~ListeningReportGateway()
{
    if (!m_client)
        return;
    disconnect(m_client, nullptr, this, nullptr);
    for (auto id : m_ids.keys())
        m_client->cancel(id);
}
quint64 ListeningReportGateway::submit(SessionToken context, QString accountId,
                                       ListeningReport report)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 8)
        return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id, {context, accountId, report});
    const auto snapshot = m_session.snapshot();
    auto error = ReportError::None;
    if (!m_session.accepts(context))
        error = ReportError::StaleContext;
    else if (snapshot.phase != SessionPhase::Authenticated)
        error = ReportError::AuthenticationRequired;
    else if (!report.isValid() || accountId != snapshot.accountId || !validMusicId(accountId))
        error = ReportError::InvalidRequest;
    else if (!m_client)
        error = ReportError::Network;
    if (error != ReportError::None) {
        QTimer::singleShot(0, this, [this, id, error] {
            ListeningReportResult result;
            result.error = error;
            finish(id, result);
        });
        return id;
    }
    const bool active = report.action == ListeningReportAction::ActiveClient;
    QJsonObject parameters;
    if (!active)
        parameters = {{QStringLiteral("id"), report.song.value},
                      {QStringLiteral("source"), sourceName(report.source)},
                      {QStringLiteral("sourceId"), report.sourceId},
                      {QStringLiteral("duration"), report.durationSeconds},
                      {QStringLiteral("quality"), report.quality},
                      {QStringLiteral("started"), report.startedAtMs}};
    if (!active && !report.program.value.isEmpty())
        parameters.insert(QStringLiteral("program"), report.program.value);
    if (!active && !report.alg.isEmpty())
        parameters.insert(QStringLiteral("alg"), report.alg);
    if (!active && report.program.value.isEmpty()) {
        parameters.insert(QStringLiteral("heart"), report.liked);
        parameters.insert(QStringLiteral("mode"), modeName(report.mode));
        parameters.insert(QStringLiteral("index"), report.index);
    }
    const bool play = report.action == ListeningReportAction::Play;
    if (play) {
        parameters.insert(QStringLiteral("time"), report.playedSeconds);
        parameters.insert(QStringLiteral("end"), endName(report.end));
    }
    log::debug(lcReport, "report.send",
               {{"id", id},
                {"event", report.eventId},
                {"action", report.action},
                {"song", report.song.value},
                log::fieldIf(!report.program.value.isEmpty(), "program", report.program.value),
                {"source", report.source},
                {"source_id", report.source == ListeningReportSource::Search
                                  ? QStringLiteral("len:%1").arg(report.sourceId.size())
                                  : report.sourceId},
                {"seconds", report.playedSeconds},
                {"mode", report.mode},
                {"liked", report.liked},
                {"duration", report.durationSeconds},
                {"quality", report.quality},
                {"end", report.end}});
    const auto apiId = m_client->submit(context,
                                        active ? ApiOperation::ListeningActive
                                        : play ? ApiOperation::ListeningPlay
                                               : ApiOperation::ListeningStart,
                                        parameters);
    m_pending[id].apiId = apiId;
    m_ids.insert(apiId, id);
    return id;
}
void ListeningReportGateway::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_pending.contains(id))
        return;
    const auto p = m_pending.value(id);
    if (p.apiId && m_client)
        m_client->cancel(p.apiId);
    else
        QTimer::singleShot(0, this, [this, id] {
            ListeningReportResult result;
            result.error = ReportError::Canceled;
            finish(id, result);
        });
}
void ListeningReportGateway::finish(quint64 id, ListeningReportResult result)
{
    if (!m_pending.contains(id))
        return;
    const auto p = m_pending.take(id);
    m_ids.remove(p.apiId);
    result.eventId = p.report.eventId;
    result.action = p.report.action;
    log::write(result.outcome == ReportOutcome::Acknowledged ? QtInfoMsg : QtWarningMsg, lcReport,
               "report.result",
               {{"id", id},
                {"event", p.report.eventId},
                {"song", p.report.song.value},
                {"outcome", result.outcome},
                {"err", result.error},
                {"code", result.providerCode ? QString::number(*result.providerCode)
                                             : QStringLiteral("-")}});
    emit completed(id, p.context, result);
}
} // namespace cirrus::netease
