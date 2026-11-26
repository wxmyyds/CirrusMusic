#include "bili_stream_resolver.h"

#include "bili_api.h"
#include "bili_quality.h"
#include "domain/music/music_types.h"
#include "infrastructure/media/media_source_policy.h"
#include "logging/log.h"

#include <QDateTime>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace cirrus::bilibili
{
using namespace domain;
using namespace application;
namespace
{
// Bilibili's playurl answers carry a 120-minute lifetime; a fresh address is one resolve away.
constexpr qint64 kUrlLifetimeMs = 120 * 60 * 1000;

Quality qualityFor(AudioQuality quality)
{
    switch (quality) {
    case AudioQuality::Standard:
        return Quality::Medium;
    case AudioQuality::Exhigh:
        return Quality::High;
    case AudioQuality::Lossless:
        return Quality::Lossless;
    case AudioQuality::HiRes:
        return Quality::HiRes;
    }
    return Quality::High;
}

// The container name AudioDecoder keys its demuxer/decoder rule on: by the provider's codecs
// and MIME, never a URL suffix.
QString formatOf(const AudioStream& stream)
{
    const auto mime = stream.mimeType.section(u';', 0, 0).trimmed().toLower();
    const auto codecs = stream.codecs.toLower();
    if (codecs.contains(QStringLiteral("flac")))
        return mime == QStringLiteral("audio/flac") ? QStringLiteral("flac")
                                                     : QStringLiteral("flac-mp4");
    if (codecs.contains(QStringLiteral("ec-3")))
        return QStringLiteral("eac3");
    if (codecs.startsWith(QStringLiteral("mp4a.")))
        return QStringLiteral("aac");
    if (!codecs.isEmpty())
        return {};
    if (mime == QStringLiteral("audio/flac") || mime == QStringLiteral("audio/x-flac"))
        return QStringLiteral("flac");
    if (mime == QStringLiteral("audio/eac3"))
        return QStringLiteral("eac3");
    if (mime == QStringLiteral("audio/mp4") || mime == QStringLiteral("video/mp4"))
        return QStringLiteral("aac");
    return {};
}

constexpr auto kBiliAutoPrefix = "bili-auto-";

// A stable cache name for this stream: the video, its page, and which track of it. Not a
// digest: a stream-addressed source is trusted once it decodes.
QString streamKeyOf(const AudioStream& stream, const QString& bvid, const QString& cid)
{
    QString part;
    if (!stream.qualityTag.trimmed().isEmpty())
        part = QStringLiteral("tag-") + stream.qualityTag.trimmed().toLower();
    else if (stream.id > 0)
        part = QStringLiteral("id-") + QString::number(stream.id);
    else
        part = QStringLiteral("quality-") + qualityKeyOf(stream) + u'-' +
               QString::number(stream.bitrateKbps);
    return QString::fromLatin1(kBiliAutoPrefix) + bvid + u'-' + cid + u'-' + part;
}

std::optional<SongTargetInfo> targetOf(const Song& song)
{
    if (song.title.trimmed().isEmpty())
        return std::nullopt;
    SongTargetInfo info;
    info.title = song.title;
    if (!song.artists.isEmpty())
        info.artist = song.artists.first().name;
    info.durationMs = song.durationMs.value_or(0);
    return info;
}
} // namespace

StreamResolver::StreamResolver(IMusicGateway& catalog, std::unique_ptr<BiliApi> api,
                               QObject* parent)
    : IStreamResolver(parent), m_catalog(catalog), m_api(std::move(api))
{
    if (m_api) {
        connect(m_api.get(), &BiliApi::resolved, this, &StreamResolver::resolved);
    }
    connect(&m_catalog, &IMusicGateway::completed, this, &StreamResolver::detailed);
}

StreamResolver::~StreamResolver()
{
    disconnect(&m_catalog, nullptr, this, nullptr);
    if (m_api)
        disconnect(m_api.get(), nullptr, this, nullptr);
    cancelAll();
}

void StreamResolver::cancelAll()
{
    const auto pending = std::exchange(m_pending, {});
    m_gatewayIds.clear();
    m_apiIds.clear();
    for (const auto& request : pending) {
        if (request.gatewayId)
            m_catalog.cancel(request.gatewayId);
        if (request.apiId && m_api)
            m_api->cancel(request.apiId);
    }
}

quint64 StreamResolver::resolve(SessionToken context, StreamQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 16)
        return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id, Pending{context, query});
    if (!DetailQuery{{query.song}}.isValid() || qualityName(query.quality).isEmpty()) {
        QTimer::singleShot(0, this, [this, id] { fail(id, PlaybackError::InvalidRequest); });
        return id;
    }
    const auto gatewayId = m_catalog.request(context, DetailQuery{{query.song}});
    if (!gatewayId) {
        QTimer::singleShot(0, this, [this, id] { fail(id, PlaybackError::Unavailable); });
        return id;
    }
    m_pending[id].gatewayId = gatewayId;
    m_gatewayIds.insert(gatewayId, id);
    return id;
}

void StreamResolver::detailed(quint64 id, SessionToken context, MusicResult result)
{
    const auto ours = m_gatewayIds.take(id);
    if (!ours)
        return;
    auto it = m_pending.find(ours);
    if (it == m_pending.end())
        return;
    it->gatewayId = 0;
    if (context != it->context) {
        fail(ours, PlaybackError::StaleContext);
        return;
    }
    const auto* details = std::get_if<SongDetails>(&result);
    if (!details) {
        const auto* failure = std::get_if<MusicFailure>(&result);
        fail(ours, failure && failure->error == MusicError::Timeout ? PlaybackError::Timeout
                                                                    : PlaybackError::Unavailable);
        return;
    }
    const auto& songId = it->query.song;
    const auto found = std::find_if(details->songs.cbegin(), details->songs.cend(),
                                    [&](const Song& song) { return song.id == songId; });
    if (found == details->songs.cend()) {
        fail(ours, PlaybackError::Unavailable); // the catalog could not describe it
        return;
    }
    const auto target = targetOf(*found);
    if (!target) {
        fail(ours, PlaybackError::Unavailable);
        return;
    }
    const auto apiId = m_api ? m_api->resolve(*target, qualityFor(it->query.quality)) : 0;
    if (!apiId) {
        fail(ours, PlaybackError::Unavailable);
        return;
    }
    it->apiId = apiId;
    m_apiIds.insert(apiId, ours);
}

void StreamResolver::resolved(quint64 apiId, BiliApi::Result result)
{
    const auto ours = m_apiIds.take(apiId);
    if (!ours)
        return;
    auto it = m_pending.find(ours);
    if (it == m_pending.end())
        return;
    if (!result.stream) {
        // A withheld song is NeedPermission; anything else is the provider simply having no
        // usable match (or a transport/parse failure), never a fallback trigger.
        fail(ours, result.needPermission ? PlaybackError::NeedPermission
                                         : PlaybackError::Unavailable);
        return;
    }
    auto& stream = *result.stream;
    if (stream.size <= 0 || stream.url.isEmpty()) {
        fail(ours, PlaybackError::InvalidResponse);
        return;
    }
    const auto format = formatOf(stream);
    if (format.isEmpty()) {
        fail(ours, PlaybackError::UnsupportedFormat);
        return;
    }
    const auto url = media::approvedMediaUrl(SourceProvider::Bilibili,
                                              QUrl(stream.url, QUrl::StrictMode));
    if (!url) {
        fail(ours, PlaybackError::UnsafeSource);
        return;
    }
    MediaSource base{*url,
                     qualityKeyOf(stream),
                     format,
                     stream.size,
                     QDeadlineTimer(kUrlLifetimeMs)};
    base.provider = SourceProvider::Bilibili;
    base.identity = StreamKey{streamKeyOf(stream, result.bvid, result.cid)};
    base.referer = QString::fromLatin1(kBiliReferer);
    base.userAgent = QString::fromLatin1(kBiliUserAgent);
    base.bili = BiliStreamMeta{result.bvid, result.cid,
                               stream.id > 0 ? QString::number(stream.id) : QString{},
                               stream.qualityTag};
    log::debug(lcApi, "bili.source",
               {{"song", it->query.song.value},
                {"bvid", result.bvid},
                {"cid", result.cid},
                {"format", format},
                {"bytes", stream.size},
                {"quality", qualityKeyOf(stream)}});
    StreamSource source;
    source.song = it->query.song;
    static_cast<MediaSource&>(source) = std::move(base);
    finish(ours, std::move(source));
}

void StreamResolver::finish(quint64 id, StreamResult result)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const auto context = it->context;
    m_pending.erase(it);
    emit completed(id, context, std::move(result));
}

void StreamResolver::fail(quint64 id, PlaybackError error)
{
    finish(id, PlaybackFailure{error});
}

void StreamResolver::cancel(quint64 id)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const auto pending = *it;
    m_pending.erase(it);
    m_gatewayIds.remove(pending.gatewayId);
    m_apiIds.remove(pending.apiId);
    if (pending.gatewayId)
        m_catalog.cancel(pending.gatewayId);
    if (pending.apiId && m_api)
        m_api->cancel(pending.apiId);
}

} // namespace cirrus::bilibili
