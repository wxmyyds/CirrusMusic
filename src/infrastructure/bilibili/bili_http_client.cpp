#include "bili_http_client.h"
#include "infrastructure/media/media_source_policy.h"
#include "logging/log.h"

#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>

namespace cirrus::bilibili
{
namespace
{
constexpr auto kOrigin = "https://www.bilibili.com";

bool apiUrl(const QUrl& url)
{
    return url.isValid() && !url.isRelative() && url.scheme() == QStringLiteral("https") &&
           (url.host() == QStringLiteral("api.bilibili.com") ||
            url.host() == QStringLiteral("www.bilibili.com")) &&
           url.port(-1) == -1 && url.userInfo().isEmpty() && !url.hasFragment() &&
           !url.path().isEmpty() && url.toEncoded().size() <= 8192;
}

qint64 resourceBytes(const QNetworkReply& reply, int status)
{
    if (status == 206) {
        static const QRegularExpression pattern(QStringLiteral("^bytes (\\d+)-(\\d+)/(\\d+)$"));
        const auto match = pattern.match(QString::fromLatin1(reply.rawHeader("Content-Range")));
        if (!match.hasMatch())
            return -1;
        bool startOk = false, endOk = false, totalOk = false;
        const auto start = match.captured(1).toLongLong(&startOk);
        const auto end = match.captured(2).toLongLong(&endOk);
        const auto total = match.captured(3).toLongLong(&totalOk);
        return startOk && endOk && totalOk && start <= end && end < total ? total : -1;
    }
    if (status == 200) {
        bool ok = false;
        const auto total = reply.rawHeader("Content-Length").trimmed().toLongLong(&ok);
        return ok && total > 0 ? total : -1;
    }
    return -1;
}
} // namespace

struct HttpClient::Pending {
    QNetworkReply* reply = nullptr;
    QTimer* timer = nullptr;
    QByteArray body;
    qsizetype limit = 0;
    bool tooLarge = false;
    bool timedOut = false;
};

HttpClient::HttpClient(QObject* parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this))
{
    m_manager->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
}

HttpClient::HttpClient(std::unique_ptr<QNetworkAccessManager> manager, QObject* parent)
    : QObject(parent), m_manager(manager.release())
{
    Q_ASSERT(m_manager);
    m_manager->setParent(this);
    m_manager->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
}

HttpClient::~HttpClient()
{
    for (const auto& pending : m_pending) {
        if (pending->reply)
            disconnect(pending->reply, nullptr, this, nullptr);
        if (pending->timer)
            disconnect(pending->timer, nullptr, this, nullptr);
    }
}

quint64 HttpClient::get(QUrl url, QByteArray cookie, bool sendCookie, int timeoutMs,
                       qsizetype responseLimit, QByteArray range)
{
    const bool api = apiUrl(url);
    if (!api && !media::approvedMediaUrl(domain::SourceProvider::Bilibili, url))
        return 0;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setRawHeader("User-Agent", kBiliUserAgent);
    request.setRawHeader("Referer", kBiliReferer);
    request.setRawHeader("Origin", kOrigin);
    request.setRawHeader("Accept", api ? "application/json" : "*/*");
    request.setRawHeader("Accept-Encoding", "identity");
    if (!range.isEmpty())
        request.setRawHeader("Range", range);
    if (api && sendCookie && !cookie.isEmpty() && !cookie.contains('\r') && !cookie.contains('\n'))
        request.setRawHeader("Cookie", cookie);

    const auto id = ++m_nextId;
    auto pending = std::make_shared<Pending>();
    pending->limit = std::clamp<qsizetype>(responseLimit, 1024, 16 * 1024 * 1024);
    pending->reply = m_manager->get(request);
    pending->timer = new QTimer(this);
    pending->timer->setSingleShot(true);
    m_pending.insert(id, pending);
    connect(pending->reply, &QNetworkReply::readyRead, this, [pending] {
        if (pending->tooLarge || !pending->reply)
            return;
        pending->body += pending->reply->read(pending->limit + 1 - pending->body.size());
        if (pending->body.size() > pending->limit) {
            pending->tooLarge = true;
            pending->body.clear();
            pending->reply->abort();
        }
    });
    connect(pending->reply, &QNetworkReply::finished, this, [this, id, pending] {
        if (!pending->reply)
            return;
        pending->timer->stop();
        auto& reply = *pending->reply;
        HttpOutcome outcome;
        outcome.status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        outcome.riskControl = outcome.status == 412;
        const auto encoding = reply.rawHeader("Content-Encoding").trimmed().toLower();
        const bool encoded = !encoding.isEmpty() && encoding != "identity";
        if (!encoded)
            outcome.resourceBytes = resourceBytes(reply, outcome.status);
        if (!pending->tooLarge)
            pending->body += reply.read(pending->limit + 1 - pending->body.size());
        pending->tooLarge = pending->tooLarge || pending->body.size() > pending->limit;
        if (outcome.riskControl) {
            outcome.error = QStringLiteral("risk-control");
        } else if (pending->timedOut || reply.error() == QNetworkReply::TimeoutError) {
            outcome.error = QStringLiteral("timeout");
        } else if (encoded) {
            outcome.error = QStringLiteral("encoding");
        } else if (pending->tooLarge) {
            outcome.error = QStringLiteral("too-large");
        } else if (reply.error() == QNetworkReply::OperationCanceledError) {
            outcome.error = QStringLiteral("canceled");
        } else if (reply.error() != QNetworkReply::NoError) {
            outcome.error = QStringLiteral("network");
        } else if (outcome.status < 200 || outcome.status >= 300) {
            outcome.error = QStringLiteral("status");
        } else {
            outcome.ok = true;
            outcome.body = std::move(pending->body);
        }
        finish(id, std::move(outcome));
    });
    connect(pending->timer, &QTimer::timeout, this, [pending] {
        if (pending->reply && !pending->reply->isFinished()) {
            pending->timedOut = true;
            pending->reply->abort();
        }
    });
    pending->timer->start(std::max(1, timeoutMs));
    return id;
}

void HttpClient::cancel(quint64 id)
{
    const auto pending = m_pending.take(id);
    if (!pending)
        return;
    if (pending->timer) {
        pending->timer->stop();
        pending->timer->deleteLater();
    }
    if (pending->reply) {
        disconnect(pending->reply, nullptr, this, nullptr);
        if (!pending->reply->isFinished())
            pending->reply->abort();
        pending->reply->deleteLater();
    }
}

void HttpClient::finish(quint64 id, HttpOutcome outcome)
{
    const auto pending = m_pending.value(id);
    if (!pending)
        return;
    pending->reply->deleteLater();
    pending->reply = nullptr;
    pending->timer->stop();
    pending->timer->deleteLater();
    pending->timer = nullptr;
    QTimer::singleShot(0, this, [this, id, outcome = std::move(outcome)]() mutable {
        if (!m_pending.remove(id))
            return;
        log::debug(lcNet, "bili.http",
                   {{"status", outcome.status}, {"ok", outcome.ok},
                    {"risk", outcome.riskControl}, {"err", outcome.error}});
        emit completed(id, std::move(outcome));
    });
}

} // namespace cirrus::bilibili
