#include "bili_api.h"
#include "bili_playurl.h"
#include "bili_wbi.h"
#include "logging/log.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>

namespace cirrus::bilibili
{
namespace
{
constexpr auto kNavUrl = "https://api.bilibili.com/x/web-interface/nav";
constexpr auto kFingerprintUrl = "https://api.bilibili.com/x/frontend/finger/spi";
constexpr auto kSearchUrl = "https://api.bilibili.com/x/web-interface/wbi/search/type";
constexpr auto kViewUrl = "https://api.bilibili.com/x/web-interface/wbi/view";
constexpr auto kPlayUrl = "https://api.bilibili.com/x/player/wbi/playurl";
constexpr qint64 kMixinTtlMs = 10 * 60 * 1000;
constexpr int kSearchLimit = 6;
constexpr int kSizeProbeTimeoutMs = 8000;

QString text(const QJsonObject& object, const char* key)
{
    const auto value = object.value(QLatin1String(key));
    return value.isString() ? value.toString() : QString{};
}

qint64 integer(const QJsonObject& object, const char* key)
{
    return object.value(QLatin1String(key)).toInteger();
}

QString plainText(QString raw)
{
    static const thread_local QRegularExpression highlight(QStringLiteral("<[^>]*>"));
    raw.remove(highlight);
    raw.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    raw.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    raw.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    raw.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    raw.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    raw.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return raw.trimmed();
}

// "mm:ss" or "h:mm:ss" to seconds; 0 when unparseable.
int durationSeconds(const QString& raw)
{
    if (raw.trimmed().isEmpty())
        return 0;
    int total = 0;
    for (const auto& part : raw.split(u':', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int piece = part.trimmed().toInt(&ok);
        if (!ok)
            return 0;
        total = total * 60 + piece;
    }
    return total;
}
} // namespace

BiliApi::BiliApi(QObject* parent) : QObject(parent), m_http(std::make_unique<HttpClient>())
{
    connect(m_http.get(), &HttpClient::completed, this, &BiliApi::onHttp);
}

BiliApi::BiliApi(std::unique_ptr<QNetworkAccessManager> manager, QObject* parent)
    : QObject(parent), m_http(std::make_unique<HttpClient>(std::move(manager)))
{
    connect(m_http.get(), &HttpClient::completed, this, &BiliApi::onHttp);
}

BiliApi::~BiliApi() = default;

void BiliApi::setCookie(QByteArray cookie)
{
    m_cookie = std::move(cookie).trimmed();
}

quint64 BiliApi::resolve(const SongTargetInfo& song, Quality preferred)
{
    if (m_requests.size() >= 16)
        return 0;
    Request request;
    request.id = ++m_nextId;
    request.song = song;
    request.preferred = preferred;
    request.queries = buildQueries(song);
    request.durationFilter = durationFilter(song.durationMs);
    const auto id = request.id;
    if (request.queries.isEmpty()) {
        m_requests.insert(id, std::move(request));
        QTimer::singleShot(0, this, [this, id] {
            fail(id, QStringLiteral("empty-query"));
        });
        return id;
    }
    // Reuse the cached WBI key and the anonymous cookie when they are still good; only the
    // search/view/playurl requests then go out. Otherwise this request fetches its own (the
    // shared caches make the next one skip straight to search).
    const bool mixinFresh = !m_mixinKey.isEmpty() &&
                            QDateTime::currentMSecsSinceEpoch() - m_mixinAtMs < kMixinTtlMs;
    request.stage = mixinFresh ? Stage::Search
                               : (effectiveCookie().isEmpty() ? Stage::Cookie : Stage::Mixin);
    m_requests.insert(id, std::move(request));
    QTimer::singleShot(0, this, [this, id] { issue(id); });
    return id;
}

void BiliApi::cancel(quint64 id)
{
    const auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    const auto httpId = it->httpId;
    m_requests.erase(it);
    m_httpIds.remove(httpId);
    if (httpId)
        m_http->cancel(httpId);
}

QByteArray BiliApi::effectiveCookie() const
{
    if (!m_cookie.isEmpty())
        return m_cookie;
    return m_buvidCookie;
}

QUrl BiliApi::signedUrl(const QUrl& url) const
{
    const auto now = QDateTime::currentSecsSinceEpoch();
    return WbiSigner::sign(url, m_mixinKey, now);
}

void BiliApi::issue(quint64 id)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    if (it->stage == Stage::Size && !it->chosen) {
        fail(id, QStringLiteral("no-stream"));
        return;
    }
    const auto stage = it->stage;
    const auto cookie = effectiveCookie();
    QUrl url;
    bool sendCookie = true;
    int timeoutMs = 15000;
    qsizetype responseLimit = 4 * 1024 * 1024;
    QByteArray range;
    switch (stage) {
    case Stage::Cookie:
        url = QUrl(QString::fromLatin1(kFingerprintUrl));
        sendCookie = false;
        break;
    case Stage::Mixin:
        url = QUrl(QString::fromLatin1(kNavUrl));
        break;
    case Stage::Search: {
        url = QUrl(QString::fromLatin1(kSearchUrl));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("search_type"), QStringLiteral("video"));
        query.addQueryItem(QStringLiteral("keyword"), it->queries.at(it->queryIndex));
        query.addQueryItem(QStringLiteral("order"), QStringLiteral("totalrank"));
        if (it->durationFilter > 0)
            query.addQueryItem(QStringLiteral("duration"), QString::number(it->durationFilter));
        query.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
        url.setQuery(query);
        url = signedUrl(url);
        break;
    }
    case Stage::View: {
        url = QUrl(QString::fromLatin1(kViewUrl));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("bvid"), it->bvid);
        url.setQuery(query);
        url = signedUrl(url);
        break;
    }
    case Stage::PlayUrl: {
        url = QUrl(QString::fromLatin1(kPlayUrl));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("bvid"), it->bvid);
        query.addQueryItem(QStringLiteral("cid"), QString::number(it->cid));
        query.addQueryItem(QStringLiteral("fnval"), QString::number(kFnvalDashDolby));
        query.addQueryItem(QStringLiteral("fnver"), QStringLiteral("0"));
        query.addQueryItem(QStringLiteral("fourk"), QStringLiteral("0"));
        query.addQueryItem(QStringLiteral("otype"), QStringLiteral("json"));
        query.addQueryItem(QStringLiteral("platform"), QStringLiteral("pc"));
        url.setQuery(query);
        url = signedUrl(url);
        break;
    }
    case Stage::Size:
        // Bilibili's CDN honors Range: one byte is enough for its Content-Range total. The
        // CDN request carries the media headers, never a Cookie.
        url = QUrl(it->chosen->url);
        sendCookie = false;
        timeoutMs = kSizeProbeTimeoutMs;
        responseLimit = 4096;
        range = QByteArrayLiteral("bytes=0-0");
        break;
    }
    const auto httpId = m_http->get(url, cookie, sendCookie, timeoutMs, responseLimit, range);
    if (!httpId) {
        fail(id, QStringLiteral("http"));
        return;
    }
    it->httpId = httpId;
    m_httpIds.insert(httpId, id);
}

void BiliApi::onHttp(quint64 httpId, HttpOutcome outcome)
{
    const auto id = m_httpIds.take(httpId);
    if (!id)
        return; // a canceled request
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    it->httpId = 0;
    if (outcome.riskControl) {
        fail(id, QStringLiteral("risk-control"));
        return;
    }
    if (!outcome.ok) {
        // The size probe only needs the byte length, which a 200-too-large abort still gave.
        if (it->stage == Stage::Size && outcome.error == QStringLiteral("too-large") &&
            outcome.status == 200 && outcome.resourceBytes > 0)
            onSize(id, std::move(outcome));
        else
            fail(id, outcome.error.isEmpty() ? QStringLiteral("http") : outcome.error);
        return;
    }
    switch (it->stage) {
    case Stage::Cookie:
        onCookie(id, std::move(outcome));
        break;
    case Stage::Mixin:
        onMixin(id, std::move(outcome));
        break;
    case Stage::Search:
        onSearch(id, std::move(outcome));
        break;
    case Stage::View:
        onView(id, std::move(outcome));
        break;
    case Stage::PlayUrl:
        onPlayUrl(id, std::move(outcome));
        break;
    case Stage::Size:
        onSize(id, std::move(outcome));
        break;
    }
}

bool BiliApi::envelope(const HttpOutcome& outcome, QJsonObject& data, QString& reason,
                       bool& needPermission) const
{
    const auto root = QJsonDocument::fromJson(outcome.body).object();
    if (root.isEmpty()) {
        reason = QStringLiteral("decode");
        return false;
    }
    if (!root.value(QStringLiteral("code")).isDouble()) {
        reason = QStringLiteral("decode");
        return false;
    }
    const auto code = integer(root, "code");
    if (code != 0) {
        reason = QStringLiteral("code:") + QString::number(code);
        // -101 not logged in; -404 not found/withheld; 62002/62004 region/permission.
        needPermission = code == -101 || code == -404 || code == 62002 || code == 62004;
        return false;
    }
    const auto value = root.value(QStringLiteral("data"));
    if (!value.isObject()) {
        reason = QStringLiteral("no-data");
        return false;
    }
    data = value.toObject();
    return true;
}

void BiliApi::onCookie(quint64 id, HttpOutcome outcome)
{
    QJsonObject data;
    QString reason;
    bool needPermission = false;
    if (!envelope(outcome, data, reason, needPermission)) {
        fail(id, reason, needPermission);
        return;
    }
    QStringList pairs;
    const auto add = [&pairs, &data](const char* key, const char* alt) {
        auto value = text(data, key);
        if (value.isEmpty())
            value = text(data, alt);
        if (!value.isEmpty())
            pairs.append(QString::fromLatin1(key) + u'=' + value);
    };
    add("buvid3", "b_3");
    add("buvid4", "b_4");
    if (!pairs.isEmpty())
        m_buvidCookie = pairs.join(QStringLiteral("; ")).toUtf8();
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    it->stage = Stage::Mixin;
    issue(id);
}

void BiliApi::onMixin(quint64 id, HttpOutcome outcome)
{
    QJsonObject data;
    QString reason;
    bool needPermission = false;
    const auto root = QJsonDocument::fromJson(outcome.body).object();
    if (root.value(QStringLiteral("code")).toInt() == -101 &&
        root.value(QStringLiteral("data")).isObject())
        data = root.value(QStringLiteral("data")).toObject();
    else if (!envelope(outcome, data, reason, needPermission)) {
        fail(id, reason, needPermission);
        return;
    }
    const auto wbi = data.value(QStringLiteral("wbi_img")).toObject();
    const auto key = WbiSigner::mixinKey(text(wbi, "img_url"), text(wbi, "sub_url"));
    if (key.isEmpty()) {
        fail(id, QStringLiteral("no-mixin"));
        return;
    }
    m_mixinKey = key;
    m_mixinAtMs = QDateTime::currentMSecsSinceEpoch();
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    it->stage = Stage::Search;
    issue(id);
}

void BiliApi::onSearch(quint64 id, HttpOutcome outcome)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    QJsonObject data;
    QString reason;
    bool needPermission = false;
    if (!envelope(outcome, data, reason, needPermission)) {
        fail(id, reason, needPermission);
        return;
    }
    const auto rows = data.value(QStringLiteral("result")).toArray();
    QList<SearchItem> items;
    for (qsizetype index = 0; index < std::min<qsizetype>(rows.size(), kSearchLimit); ++index) {
        const auto row = rows.at(index).toObject();
        if (text(row, "type") != QStringLiteral("video"))
            continue;
        SearchItem item;
        item.aid = integer(row, "aid");
        item.bvid = text(row, "bvid");
        item.title = plainText(text(row, "title"));
        item.author = plainText(text(row, "author"));
        item.durationSec = durationSeconds(text(row, "duration"));
        item.play = integer(row, "play");
        if (item.bvid.isEmpty() || item.title.isEmpty())
            continue;
        items.append(item);
    }
    const auto& song = it->song;
    std::stable_sort(items.begin(), items.end(), [&song](const SearchItem& a, const SearchItem& b) {
        return score(song, a.title, a.author, a.durationSec) >
               score(song, b.title, b.author, b.durationSec);
    });
    if (items.size() > kSearchLimit)
        items = items.mid(0, kSearchLimit);
    it->candidates = items;
    it->candidateIndex = 0;
    nextCandidate(id);
}

void BiliApi::nextCandidate(quint64 id)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    while (it->candidateIndex < it->candidates.size()) {
        const auto& candidate = it->candidates.at(it->candidateIndex);
        if (candidate.bvid.isEmpty()) {
            ++it->candidateIndex;
            continue;
        }
        it->bvid = candidate.bvid;
        it->stage = Stage::View;
        issue(id);
        return;
    }
    // Out of candidates for this query: the next query, or no match.
    ++it->queryIndex;
    nextQuery(id);
}

void BiliApi::nextQuery(quint64 id)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    if (it->queryIndex >= it->queries.size()) {
        fail(id, QStringLiteral("no-match"));
        return;
    }
    it->candidateIndex = 0;
    it->stage = Stage::Search;
    issue(id);
}

void BiliApi::onView(quint64 id, HttpOutcome outcome)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    QJsonObject data;
    QString reason;
    bool needPermission = false;
    if (!envelope(outcome, data, reason, needPermission)) {
        fail(id, reason, needPermission);
        return;
    }
    VideoInfo info;
    info.aid = integer(data, "aid");
    info.bvid = text(data, "bvid");
    info.title = plainText(text(data, "title"));
    info.ownerName = text(data.value(QStringLiteral("owner")).toObject(), "name");
    info.durationSec = integer(data, "duration");
    for (const auto& value : data.value(QStringLiteral("pages")).toArray()) {
        const auto page = value.toObject();
        VideoPage parsed;
        parsed.cid = integer(page, "cid");
        parsed.index = integer(page, "page");
        parsed.part = page.value(QStringLiteral("part")).toString();
        parsed.durationSec = integer(page, "duration");
        if (parsed.cid > 0)
            info.pages.append(parsed);
    }
    const auto& candidate = it->candidates.at(it->candidateIndex);
    const auto matched = bestPage(it->song, candidate, info);
    if (!matched || matched->score < kMinimumScore) {
        log::debug(lcApi, "bili.match_skip",
                   {{"bvid", candidate.bvid}, {"score", matched ? matched->score : 0}});
        ++it->candidateIndex;
        nextCandidate(id);
        return;
    }
    it->bvid = info.bvid.isEmpty() ? candidate.bvid : info.bvid;
    it->cid = matched->page.cid;
    it->stage = Stage::PlayUrl;
    log::debug(lcApi, "bili.match", {{"bvid", it->bvid}, {"score", matched->score}});
    issue(id);
}

void BiliApi::onPlayUrl(quint64 id, HttpOutcome outcome)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    QJsonObject data;
    QString reason;
    bool needPermission = false;
    if (!envelope(outcome, data, reason, needPermission)) {
        fail(id, reason, needPermission);
        return;
    }
    const auto mapping = mapPlayUrl(data);
    const auto stream = selectStream(mapping.result.streams, it->preferred);
    if (!stream) {
        fail(id, QStringLiteral("no-audio"), needPermission);
        return;
    }
    if (stream->size > 0) {
        succeed(id, *stream); // the answer carried the length: no probe needed
        return;
    }
    it->chosen = *stream;
    it->stage = Stage::Size;
    issue(id);
}

void BiliApi::onSize(quint64 id, HttpOutcome outcome)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end() || !it->chosen)
        return;
    auto& stream = *it->chosen;
    // A 206's Content-Range total is exact; a 200's Content-Length is the whole resource even
    // when the body was capped and the read aborted. Either way the outcome carries it.
    if (outcome.resourceBytes > 0)
        stream.size = outcome.resourceBytes;
    if (stream.size <= 0) {
        fail(id, QStringLiteral("no-size"));
        return;
    }
    succeed(id, stream);
}

void BiliApi::fail(quint64 id, const QString& reason, bool needPermission)
{
    Result result;
    result.failure = reason;
    result.needPermission = needPermission;
    log::debug(lcApi, "bili.resolve_failed", {{"reason", reason}, {"perm", needPermission}});
    complete(id, std::move(result));
}

void BiliApi::succeed(quint64 id, AudioStream stream)
{
    const auto it = m_requests.constFind(id);
    if (it == m_requests.constEnd())
        return;
    Result result;
    result.stream = std::move(stream);
    result.bvid = it->bvid;
    result.cid = QString::number(it->cid);
    log::debug(lcApi, "bili.resolve_ok",
               {{"bvid", result.bvid},
                {"cid", result.cid},
                {"bitrate", result.stream->bitrateKbps},
                {"quality", qualityKeyOf(*result.stream)}});
    complete(id, std::move(result));
}

void BiliApi::complete(quint64 id, Result result)
{
    const auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;
    if (it->httpId)
        m_httpIds.remove(it->httpId);
    m_requests.erase(it);
    emit resolved(id, std::move(result));
}

} // namespace cirrus::bilibili
