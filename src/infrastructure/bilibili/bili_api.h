#pragma once
#include "bili_http_client.h"
#include "bili_matcher.h"
#include "bili_models.h"
#include "bili_quality.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace cirrus::bilibili
{

// The Bilibili web API as one async object: an anonymous cookie (buvid3/4) or a configured
// login cookie, the WBI mixin key, then search → view → playurl per song, and a Range probe
// for the chosen stream's byte length (the playurl answer seldom carries it). Requests run
// side by side — the player preloads the next song while one plays — each with its own chain
// and one completion. A failure is typed and final (no retry loop, no provider switch).
// Cookies go only to api.bilibili.com; the chosen CDN address travels on in the result.
class BiliApi final : public QObject
{
    Q_OBJECT
  public:
    struct Result {
        std::optional<AudioStream> stream; // `stream->size` is exact once this is set
        QString bvid;
        QString cid;
        QString failure;             // short, log-safe reason ("search", "code:-400", ...)
        bool needPermission = false; // the provider withheld it (login/VIP/region)
        friend bool operator==(const Result&, const Result&) = default;
    };

    explicit BiliApi(QObject* parent = nullptr);
    explicit BiliApi(std::unique_ptr<QNetworkAccessManager> manager, QObject* parent = nullptr);
    ~BiliApi() override;

    // A configured login cookie ("k=v; k2=v2"); empty uses an anonymous buvid cookie.
    void setCookie(QByteArray cookie);
    [[nodiscard]] bool hasCookie() const { return !m_cookie.isEmpty(); }

    // Resolve `song` to one stream at `preferred`. `preferred` is our own quality setting.
    // Nonzero request id; completion (success or typed failure) is always deferred.
    quint64 resolve(const SongTargetInfo& song, Quality preferred);
    void cancel(quint64 id);

  signals:
    void resolved(quint64 id, cirrus::bilibili::BiliApi::Result result);

  private:
    enum class Stage { Cookie, Mixin, Search, View, PlayUrl, Size };
    struct Request {
        quint64 id = 0;
        SongTargetInfo song;
        Quality preferred = Quality::High;
        QStringList queries;
        qsizetype queryIndex = 0;
        QList<SearchItem> candidates;
        qsizetype candidateIndex = 0;
        QString bvid;
        qint64 cid = 0;
        int durationFilter = 0;
        Stage stage = Stage::Search;
        quint64 httpId = 0;
        std::optional<AudioStream> chosen; // at Stage::Size, the stream awaiting its length
    };

    void issue(quint64 id);
    void onHttp(quint64 httpId, HttpOutcome outcome);
    void onCookie(quint64 id, HttpOutcome outcome);
    void onMixin(quint64 id, HttpOutcome outcome);
    void onSearch(quint64 id, HttpOutcome outcome);
    void onView(quint64 id, HttpOutcome outcome);
    void onPlayUrl(quint64 id, HttpOutcome outcome);
    void onSize(quint64 id, HttpOutcome outcome);
    // code == 0 and `data` present; else sets `reason`/`needPermission`.
    bool envelope(const HttpOutcome& outcome, QJsonObject& data, QString& reason,
                  bool& needPermission) const;
    void nextCandidate(quint64 id);
    void nextQuery(quint64 id);
    void fail(quint64 id, const QString& reason, bool needPermission = false);
    void succeed(quint64 id, AudioStream stream);
    void complete(quint64 id, Result result);
    [[nodiscard]] QByteArray effectiveCookie() const;
    [[nodiscard]] QUrl signedUrl(const QUrl& url) const;

    std::unique_ptr<HttpClient> m_http;
    QByteArray m_cookie;      // configured login cookie; empty when anonymous
    QByteArray m_buvidCookie; // anonymous buvid3/buvid4, fetched once
    QString m_mixinKey;
    qint64 m_mixinAtMs = 0;
    QHash<quint64, Request> m_requests;    // ours
    QHash<quint64, quint64> m_httpIds;     // HTTP id -> our request id
    quint64 m_nextId = 0;
};

} // namespace cirrus::bilibili

Q_DECLARE_METATYPE(cirrus::bilibili::BiliApi::Result)
