// Official-only adaptation of api-enhanced module/song_url_v1.js (MIT).
// No unblock branch. See third_party/api-enhanced/NOTICE.md.
#include "stream_resolver.h"
#include "infrastructure/media/media_source_policy.h"
#include <QJsonArray>
#include <QThread>
#include <QVariant>
#include <algorithm>
#include <cmath>

namespace cirrus::netease
{
using namespace domain;
using namespace application;
namespace
{
std::optional<qint64> integer(const QJsonValue& value, qint64 maximum)
{
    if (value.toVariant().metaType().id() != QMetaType::LongLong)
        return std::nullopt;
    const auto n = value.toInteger();
    return n >= 0 && n <= maximum ? std::optional<qint64>(n) : std::nullopt;
}
QString idText(const QJsonValue& value)
{
    if (value.isString() && validMusicId(value.toString()))
        return value.toString();
    const auto n = integer(value, 9007199254740991LL);
    return n && *n > 0 ? QString::number(*n) : QString{};
}
std::optional<qint64> secondsMs(const QJsonValue& value)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0 ||
        value.toDouble() > 7 * 24 * 60 * 60)
        return std::nullopt;
    return qRound64(value.toDouble() * 1000);
}
} // namespace
StreamResult mapStreamResponse(const StreamQuery& query, const QJsonObject& object, qint64 elapsed)
{
    const PlaybackFailure invalid{PlaybackError::InvalidResponse};
    const auto rootCode = integer(object.value(QStringLiteral("code")), 99999);
    if (!rootCode)
        return invalid;
    if (*rootCode != 200)
        return PlaybackFailure{*rootCode == 429 ? PlaybackError::RateLimited
                                                : PlaybackError::Unavailable,
                               int(*rootCode)};
    const auto array = object.value(QStringLiteral("data")).toArray();
    if (array.size() != 1 || !array.first().isObject())
        return invalid;
    const auto row = array.first().toObject();
    if (!validMusicId(query.song.value) ||
        idText(row.value(QStringLiteral("id"))) != query.song.value)
        return invalid;
    const auto code = integer(row.value(QStringLiteral("code")), 99999);
    if (!code)
        return invalid;
    if (*code != 200)
        return PlaybackFailure{*code == 404 ? PlaybackError::NeedPermission
                                            : PlaybackError::Unavailable,
                               int(*code)};
    // The provider's own verdict on entitlement, read from the same row: a positive fee or
    // cannotListenReason 1 means it has the song but withholds it (VIP, region, restriction).
    // This is a semantic change: fee is now read, not ignored. Any other empty address (a
    // network/expiry symptom) stays Unavailable.
    const auto denyReason = [&row] {
        const auto fee = integer(row.value(QStringLiteral("fee")), 1000);
        if (fee && *fee > 0)
            return true;
        const auto reason = integer(
            row.value(QStringLiteral("freeTrialPrivilege"))
                .toObject()
                .value(QStringLiteral("cannotListenReason")),
            1000);
        return reason && *reason == 1;
    };
    const auto urlField = row.value(QStringLiteral("url"));
    if (urlField.isNull() || (urlField.isString() && urlField.toString().isEmpty()))
        return PlaybackFailure{denyReason() ? PlaybackError::NeedPermission
                                            : PlaybackError::Unavailable,
                               int(*code)};
    if (!urlField.isString())
        return invalid;
    const auto url = media::approvedMediaUrl(domain::SourceProvider::Netease,
                                             QUrl(urlField.toString(), QUrl::StrictMode), true);
    if (!url)
        return PlaybackFailure{PlaybackError::UnsafeSource};
    const auto ttl = integer(row.value(QStringLiteral("expi")), 86400);
    const auto size = integer(row.value(QStringLiteral("size")), 1024LL * 1024 * 1024);
    if (!ttl || !size || *size == 0 || elapsed < 0)
        return invalid;
    if (*ttl * 1000 <= elapsed)
        return PlaybackFailure{PlaybackError::Expired};
    const auto quality = row.value(QStringLiteral("level")).toString();
    if (quality.isEmpty() || quality.size() > 32)
        return invalid;
    for (const auto c : quality)
        if (c < u'a' || c > u'z')
            return invalid;
    const auto format = row.value(QStringLiteral("type")).toString().toLower();
    if (format != QStringLiteral("mp3") && format != QStringLiteral("flac"))
        return PlaybackFailure{PlaybackError::UnsupportedFormat};
    StreamSource source{query.song, *url,  quality,
                        format,     *size, QDeadlineTimer(*ttl * 1000 - elapsed)};
    // Optional; a malformed digest is dropped (no cache), not a failure.
    const auto md5 = row.value(QStringLiteral("md5")).toString().toLower().toLatin1();
    if (md5.size() == 32 && std::all_of(md5.begin(), md5.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))
        source.identity = ContentHash{md5};
    // Optional loudness (docs/tasks/audio-engine.md §6.5): measured 23 full tracks,
    // integrated loudness + gain = −17.96 ± 0.04 LUFS. A peak of 0 is the placeholder
    // preview clips carry; an odd value is dropped, not a failure.
    const auto gain = row.value(QStringLiteral("gain"));
    const auto peak = row.value(QStringLiteral("peak"));
    if (gain.isDouble() && peak.isDouble() && std::isfinite(gain.toDouble()) &&
        std::abs(gain.toDouble()) <= 40 && peak.toDouble() > 0 && peak.toDouble() <= 2) {
        source.gainDb = float(gain.toDouble());
        source.peak = float(std::min(peak.toDouble(), 1.0));
    }
    const auto preview = row.value(QStringLiteral("freeTrialInfo"));
    if (!preview.isUndefined() && !preview.isNull()) {
        if (!preview.isObject())
            return invalid;
        const auto start = secondsMs(preview.toObject().value(QStringLiteral("start")));
        const auto end = secondsMs(preview.toObject().value(QStringLiteral("end")));
        if (!start || !end || *end <= *start)
            return invalid;
        source.preview = PreviewRange{*start, *end};
    }
    // fee/payed/advertised privileges: fee and the cannotListenReason already decided
    // NeedPermission above; the rest neither grant nor deny this source.
    // Absence of preview metadata is NOT proof of full-track/download entitlement.
    return source;
}
StreamResolver::StreamResolver(SessionApiClient& client, QObject* parent)
    : IStreamResolver(parent), m_client(client)
{
    Q_ASSERT(client.thread() == thread());
    connect(&client, &SessionApiClient::completed, this,
            [this](quint64 id, SessionToken token, ApiResult result) {
                if (!m_pending.contains(id))
                    return;
                const auto p = m_pending.take(id);
                StreamResult mapped = PlaybackFailure{PlaybackError::Network};
                if (p.context != token || !m_client.accepts(p.context) ||
                    result.error == ApiError::StaleContext)
                    mapped = PlaybackFailure{PlaybackError::StaleContext};
                else if (result.error == ApiError::None)
                    mapped = mapStreamResponse(p.query, result.decoded.object, p.age.elapsed());
                else if (result.error == ApiError::MissingProtocolKey)
                    mapped = PlaybackFailure{PlaybackError::ProtocolKeyMissing};
                else if (result.error == ApiError::InvalidRequest)
                    mapped = PlaybackFailure{PlaybackError::InvalidRequest};
                else if (result.error == ApiError::Canceled)
                    mapped = PlaybackFailure{PlaybackError::Canceled};
                else if (result.error == ApiError::Decode || result.error == ApiError::Crypto)
                    mapped = PlaybackFailure{PlaybackError::InvalidResponse};
                else if (result.transportError == TransportError::Timeout)
                    mapped = PlaybackFailure{PlaybackError::Timeout};
                else if (result.httpStatus == 429)
                    mapped = PlaybackFailure{PlaybackError::RateLimited};
                else if (result.httpStatus == 401 || result.httpStatus == 403)
                    mapped = PlaybackFailure{PlaybackError::Unavailable};
                emit completed(id, p.context, std::move(mapped));
            });
}
StreamResolver::~StreamResolver()
{
    disconnect(&m_client, nullptr, this, nullptr);
    for (auto id : m_pending.keys())
        m_client.cancel(id);
}
quint64 StreamResolver::resolve(SessionToken context, StreamQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 16)
        return 0;
    const auto id = m_client.submit(context, ApiOperation::SongStream,
                                    {{QStringLiteral("id"), query.song.value},
                                     {QStringLiteral("level"), qualityName(query.quality)}});
    Pending pending{context, std::move(query), {}};
    pending.age.start();
    m_pending.insert(id, std::move(pending));
    return id;
}
void StreamResolver::cancel(quint64 id) { m_client.cancel(id); }
} // namespace cirrus::netease
