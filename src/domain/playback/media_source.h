#pragma once
#include <QByteArray>
#include <QDeadlineTimer>
#include <QString>
#include <QUrl>
#include <optional>
#include <variant>

namespace cirrus::domain
{
struct PreviewRange {
    qint64 startMs = 0;
    qint64 endMs = 0;
};

// Which provider issued a source. Keys the media URL policy, the cache identity and (later)
// the lyrics path; nothing else may infer a provider from the address.
enum class SourceProvider { Netease, Bilibili };

// Content-addressed identity: the provider's digest (32 lowercase hex). The cache keys on it
// plus the file size, and checks a finished file against it. Empty md5: nothing to key on.
struct ContentHash {
    QByteArray md5;
    friend bool operator==(const ContentHash&, const ContentHash&) = default;
};

// Stream-addressed identity (Bilibili): no digest, only the provider's stable name for this
// stream. A finished file is trusted once it decodes; a damaged one is downloaded again.
// Never a pseudo-digest.
struct StreamKey {
    QString key;
    friend bool operator==(const StreamKey&, const StreamKey&) = default;
};

// What the stored Bilibili stream is: enough for the lyrics path and the source marker.
// No URL and no credential.
struct BiliStreamMeta {
    QString bvid;
    QString cid;
    QString streamId;
    QString qualityTag; // "hires", "dolby" or empty
    friend bool operator==(const BiliStreamMeta&, const BiliStreamMeta&) = default;
};

// Physical, transient payload ONLY. No song/program/voice/track/account identity.
// Signed URLs never enter UI snapshots/logs; metadata is not a permission grant.
struct MediaSource {
    QUrl url;
    QString actualQuality;
    QString format;
    qint64 size = 0;
    QDeadlineTimer validUntil;
    std::optional<PreviewRange> preview;
    // Loudness from the provider: the ReplayGain 2 track gain (dB to −18 LUFS) and the
    // sample peak (0–1, capped at 1). Absent when it gave none or only a placeholder
    // (preview clips say gain 1.5, peak 0). Physical too: for volume normalization only.
    std::optional<float> gainDb;
    float peak = 0;
    // Which provider issued this, and what keys its cache entry. One or the other, never both.
    SourceProvider provider = SourceProvider::Netease;
    std::variant<ContentHash, StreamKey> identity = ContentHash{};
    // Provider request headers for the CDN fetch: set only by Bilibili (Referer and UA).
    // Never a Cookie; the store sends them only for that provider.
    QString referer;
    QString userAgent;
    std::optional<BiliStreamMeta> bili;
};

// The provider's digest for a content-addressed source, empty for a stream-addressed one.
inline QByteArray contentMd5(const MediaSource& source)
{
    if (const auto* hash = std::get_if<ContentHash>(&source.identity))
        return hash->md5;
    return {};
}

// Whether the source can be keyed in the cache (content digest or stream name present).
inline bool hasIdentity(const MediaSource& source)
{
    if (const auto* hash = std::get_if<ContentHash>(&source.identity))
        return !hash->md5.isEmpty();
    if (const auto* key = std::get_if<StreamKey>(&source.identity))
        return !key->key.isEmpty();
    return false;
}
} // namespace cirrus::domain

Q_DECLARE_METATYPE(cirrus::domain::SourceProvider)
Q_DECLARE_METATYPE(cirrus::domain::ContentHash)
Q_DECLARE_METATYPE(cirrus::domain::StreamKey)
Q_DECLARE_METATYPE(cirrus::domain::BiliStreamMeta)
