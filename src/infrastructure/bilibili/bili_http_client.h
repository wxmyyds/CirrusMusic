#pragma once
#include <QByteArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QUrl>

#include <memory>

namespace cirrus::bilibili
{

// The desktop web identity every Bilibili endpoint and CDN request needs. One place, so the
// API client and the media source's headers cannot drift.
inline constexpr auto kBiliReferer = "https://www.bilibili.com/";
inline constexpr auto kBiliUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

// What one GET ended as. Unlike the NetEase transport this speaks plain public JSON, so a
// provider `code` travels inside the body; only transport-level facts live here.
struct HttpOutcome {
    bool ok = false;        // a body arrived with a 2xx status
    bool riskControl = false; // HTTP 412: Bilibili's anti-bot wall, never retried
    int status = 0;
    QByteArray body;
    QString error; // a short, log-safe reason ("timeout", "network", "too-large", ...)
    // From the reply headers, for the size probe: the whole resource's byte length. A 206
    // "bytes a-b/total" and a 200 Content-Length both fill it; -1 when neither did.
    qint64 resourceBytes = -1;
};

// Thread-affine async GET with the headers every Bilibili endpoint expects. Cookie is only
// ever attached to api/www calls (this client's callers pass it); it is never sent to a CDN.
// One request, one completion; no retry, no redirect following.
class HttpClient final : public QObject
{
    Q_OBJECT
  public:
    explicit HttpClient(QObject* parent = nullptr);
    explicit HttpClient(std::unique_ptr<QNetworkAccessManager> manager, QObject* parent = nullptr);
    ~HttpClient() override;

    // `cookie` (may be empty) is added only when `sendCookie` is true. Redirects are refused:
    // a provider answer's address is followed by AudioStore under its own policy. A non-empty
    // `range` (e.g. "bytes=0-0") asks only that span, for the size probe.
    quint64 get(QUrl url, QByteArray cookie, bool sendCookie, int timeoutMs = 15000,
                qsizetype responseLimit = 4 * 1024 * 1024, QByteArray range = {});
    void cancel(quint64 id);

  signals:
    void completed(quint64 id, cirrus::bilibili::HttpOutcome outcome);

  private:
    struct Pending;
    void finish(quint64 id, HttpOutcome outcome);
    QNetworkAccessManager* m_manager = nullptr;
    QHash<quint64, std::shared_ptr<Pending>> m_pending;
    quint64 m_nextId = 0;
};

} // namespace cirrus::bilibili

Q_DECLARE_METATYPE(cirrus::bilibili::HttpOutcome)
