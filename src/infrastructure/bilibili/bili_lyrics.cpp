#include "bili_lyrics.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrlQuery>

#include <cmath>

namespace cirrus::bilibili
{
using namespace domain;
using namespace application;
namespace
{
bool trustedSubtitle(const QUrl& url)
{
    const auto host = url.host();
    return url.isValid() && !url.isRelative() && url.scheme() == QStringLiteral("https") &&
           url.userInfo().isEmpty() && url.port(-1) == -1 && !url.hasFragment() &&
           !url.path().isEmpty() && url.toEncoded().size() <= 8192 &&
           (host == QStringLiteral("hdslb.com") || host.endsWith(QStringLiteral(".hdslb.com")));
}

MusicFailure failureOf(const HttpOutcome& outcome)
{
    return {outcome.error == QStringLiteral("timeout") ? MusicError::Timeout : MusicError::Network};
}
} // namespace

std::optional<QUrl> subtitleUrl(const QJsonObject& data)
{
    const auto rows = data.value(QStringLiteral("subtitle")).toObject()
                          .value(QStringLiteral("subtitles")).toArray();
    std::optional<QUrl> chosen;
    int best = -1;
    for (const auto& value : rows) {
        const auto row = value.toObject();
        auto raw = row.value(QStringLiteral("subtitle_url")).toString();
        if (raw.startsWith(QStringLiteral("//")))
            raw.prepend(QStringLiteral("https:"));
        const QUrl url(raw, QUrl::StrictMode);
        if (!trustedSubtitle(url))
            continue;
        const auto language = row.value(QStringLiteral("lan")).toString();
        const int score = language == QStringLiteral("zh-CN") ? 3
                          : language.startsWith(QStringLiteral("zh")) ? 2
                          : language.startsWith(QStringLiteral("ai-zh")) ? 1 : 0;
        if (score > best) {
            best = score;
            chosen = url;
        }
    }
    return chosen;
}

LyricTrack subtitleTrack(const QJsonObject& data)
{
    LyricTrack track;
    const auto body = data.value(QStringLiteral("body"));
    if (!body.isArray()) {
        track.availability = LyricAvailability::Malformed;
        return track;
    }
    track.timing = LyricTiming::Lines;
    const auto rows = body.toArray();
    if (rows.size() > 10000) {
        track.availability = LyricAvailability::Malformed;
        return track;
    }
    for (const auto& value : rows) {
        const auto row = value.toObject();
        const auto from = row.value(QStringLiteral("from"));
        const auto to = row.value(QStringLiteral("to"));
        const auto content = row.value(QStringLiteral("content"));
        const double start = from.toDouble(-1);
        const double end = to.toDouble(-1);
        if (!from.isDouble() || !to.isDouble() || !std::isfinite(start) || !std::isfinite(end) ||
            start < 0 || end <= start || end > ObservedPlayback::MaximumMs / 1000 ||
            !content.isString() || content.toString().size() > 16384 ||
            content.toString().contains(QChar::Null)) {
            ++track.malformedLines;
            continue;
        }
        const auto startMs = qint64(std::llround(start * 1000));
        const auto endMs = qint64(std::llround(end * 1000));
        if (endMs <= startMs) {
            ++track.malformedLines;
            continue;
        }
        if (!content.toString().trimmed().isEmpty())
            track.lines.append({startMs, endMs, content.toString(), {}});
    }
    track.availability = !track.lines.isEmpty() ? LyricAvailability::Present
                         : track.malformedLines ? LyricAvailability::Malformed
                                                : LyricAvailability::Empty;
    return track;
}

Lyrics::Lyrics(QObject* parent) : QObject(parent), m_http(std::make_unique<HttpClient>())
{
    connect(m_http.get(), &HttpClient::completed, this, &Lyrics::received);
}

Lyrics::Lyrics(std::unique_ptr<QNetworkAccessManager> manager, QObject* parent)
    : QObject(parent), m_http(std::make_unique<HttpClient>(std::move(manager)))
{
    connect(m_http.get(), &HttpClient::completed, this, &Lyrics::received);
}

quint64 Lyrics::request(PlaybackSourceSelection source)
{
    if (m_pending.size() >= 16)
        return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id, {std::move(source)});
    QTimer::singleShot(0, this, [this, id] {
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            return;
        if (!PlaybackLyricQuery{it->source}.isValid() ||
            it->source.provider != SourceProvider::Bilibili) {
            finish(id, MusicFailure{MusicError::InvalidRequest});
            return;
        }
        QUrl url(QStringLiteral("https://api.bilibili.com/x/player/v2"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("bvid"), it->source.bili->bvid);
        query.addQueryItem(QStringLiteral("cid"), it->source.bili->cid);
        url.setQuery(query);
        const auto httpId = m_http->get(url, m_cookie, true);
        if (!httpId) {
            finish(id, MusicFailure{MusicError::InvalidRequest});
            return;
        }
        it->httpId = httpId;
        m_httpIds.insert(httpId, id);
    });
    return id;
}

void Lyrics::received(quint64 httpId, HttpOutcome outcome)
{
    const auto id = m_httpIds.take(httpId);
    auto it = m_pending.find(id);
    if (!id || it == m_pending.end())
        return;
    it->httpId = 0;
    if (!outcome.ok) {
        finish(id, failureOf(outcome));
        return;
    }
    const auto document = QJsonDocument::fromJson(outcome.body);
    if (!document.isObject()) {
        finish(id, MusicFailure{MusicError::InvalidResponse});
        return;
    }
    const auto root = document.object();
    if (it->subtitle) {
        SongLyrics lyrics;
        lyrics.song = it->source.song;
        lyrics.original.standard = subtitleTrack(root);
        if (lyrics.original.standard.availability == LyricAvailability::Malformed)
            finish(id, MusicFailure{MusicError::InvalidResponse});
        else
            finish(id, lyrics);
        return;
    }
    if (!root.value(QStringLiteral("code")).isDouble() ||
        root.value(QStringLiteral("code")).toDouble() != 0 ||
        !root.value(QStringLiteral("data")).isObject()) {
        finish(id, MusicFailure{MusicError::ProviderRejected});
        return;
    }
    const auto data = root.value(QStringLiteral("data")).toObject();
    const auto subtitle = data.value(QStringLiteral("subtitle"));
    const auto rows = subtitle.toObject().value(QStringLiteral("subtitles"));
    if ((!subtitle.isUndefined() && !subtitle.isObject()) ||
        (!rows.isUndefined() && !rows.isArray())) {
        finish(id, MusicFailure{MusicError::InvalidResponse});
        return;
    }
    const auto url = subtitleUrl(data);
    if (!url) {
        if (!rows.toArray().isEmpty()) {
            finish(id, MusicFailure{MusicError::InvalidResponse});
            return;
        }
        SongLyrics missing;
        missing.song = it->source.song;
        missing.providerNoLyric = true;
        finish(id, missing);
        return;
    }
    const auto requestId = m_http->get(*url, {}, false, 15000, 4 * 1024 * 1024, {},
                                      HttpScope::Subtitle);
    if (!requestId) {
        finish(id, MusicFailure{MusicError::InvalidRequest});
        return;
    }
    it->subtitle = true;
    it->httpId = requestId;
    m_httpIds.insert(requestId, id);
}

void Lyrics::cancel(quint64 id)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const auto httpId = it->httpId;
    m_pending.erase(it);
    m_httpIds.remove(httpId);
    if (httpId)
        m_http->cancel(httpId);
}

void Lyrics::finish(quint64 id, MusicResult result)
{
    if (!m_pending.remove(id))
        return;
    emit completed(id, std::move(result));
}
} // namespace cirrus::bilibili
