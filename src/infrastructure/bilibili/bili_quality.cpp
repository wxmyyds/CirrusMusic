#include "bili_quality.h"

#include <QSet>
#include <QUrl>
#include <algorithm>
#include <limits>

namespace cirrus::bilibili
{
namespace
{
int minBitrateKbps(Quality quality)
{
    switch (quality) {
    case Quality::HiRes:
        return 1000;
    case Quality::Lossless:
        return 500;
    case Quality::High:
        return 180;
    case Quality::Medium:
        return 120;
    case Quality::Low:
        return 60;
    }
    return 180;
}

// Exclusive upper bound of a regular band, so a stream belongs to exactly one step.
int upperBoundExclusive(Quality quality)
{
    switch (quality) {
    case Quality::Lossless:
        return minBitrateKbps(Quality::HiRes);
    case Quality::High:
        return minBitrateKbps(Quality::Lossless);
    case Quality::Medium:
        return minBitrateKbps(Quality::High);
    case Quality::Low:
        return minBitrateKbps(Quality::Medium);
    case Quality::HiRes:
        break;
    }
    return std::numeric_limits<int>::max();
}

QString normalizedTag(const AudioStream& stream) { return stream.qualityTag.trimmed().toLower(); }

bool matchesRegularBand(const AudioStream& stream, Quality quality)
{
    if (!normalizedTag(stream).isEmpty())
        return false;
    return stream.bitrateKbps >= minBitrateKbps(quality) &&
           stream.bitrateKbps < upperBoundExclusive(quality);
}

bool isLosslessLike(const AudioStream& stream)
{
    const auto tag = normalizedTag(stream);
    if (tag == QStringLiteral("lossless") || tag == QStringLiteral("hires"))
        return true;
    auto mime = stream.mimeType;
    if (const auto semi = mime.indexOf(u';'); semi >= 0)
        mime = mime.left(semi);
    mime = mime.trimmed().toLower();
    return mime == QStringLiteral("audio/flac") || mime == QStringLiteral("audio/x-flac");
}

QList<Quality> degradeChain(Quality from)
{
    static const QList<Quality> order{Quality::HiRes, Quality::Lossless, Quality::High,
                                      Quality::Medium, Quality::Low};
    const auto at = order.indexOf(from);
    return order.mid(at < 0 ? 0 : at);
}

int hostScore(const QString& url)
{
    const auto host = QUrl(url).host().toLower();
    const auto bilivideo = host.endsWith(QStringLiteral("bilivideo.com")) ||
                           host.endsWith(QStringLiteral("bilivideo.cn"));
    if (host.startsWith(QStringLiteral("upos-")) && bilivideo)
        return 3;
    if (bilivideo)
        return 2;
    if (host.endsWith(QStringLiteral("mountaintoys.cn")))
        return 1;
    return 0;
}
} // namespace

Quality qualityFromKey(const QString& key)
{
    const auto k = key.trimmed().toLower();
    if (k == QStringLiteral("hires"))
        return Quality::HiRes;
    if (k == QStringLiteral("lossless"))
        return Quality::Lossless;
    if (k == QStringLiteral("medium"))
        return Quality::Medium;
    if (k == QStringLiteral("low"))
        return Quality::Low;
    return Quality::High;
}

std::optional<AudioStream> selectStream(const QList<AudioStream>& available, Quality preferred)
{
    if (available.isEmpty())
        return std::nullopt;

    QList<AudioStream> regular;
    QList<AudioStream> tagged;
    for (const auto& stream : available) {
        if (normalizedTag(stream).isEmpty())
            regular.append(stream);
        else
            tagged.append(stream);
    }
    const auto byBitrate = [](const AudioStream& a, const AudioStream& b) {
        return a.bitrateKbps > b.bitrateKbps;
    };
    std::sort(regular.begin(), regular.end(), byBitrate);
    std::sort(tagged.begin(), tagged.end(), byBitrate);

    QList<AudioStream> sorted;
    QSet<QString> seen;
    for (const auto& stream : regular + tagged)
        if (!seen.contains(stream.url)) {
            seen.insert(stream.url);
            sorted.append(stream);
        }

    const auto taggedFirst = [&sorted](const QString& tag) -> std::optional<AudioStream> {
        for (const auto& stream : sorted)
            if (normalizedTag(stream) == tag)
                return stream;
        return std::nullopt;
    };
    const auto losslessFirst = [&sorted]() -> std::optional<AudioStream> {
        for (const auto& stream : sorted)
            if (isLosslessLike(stream))
                return stream;
        return std::nullopt;
    };

    if (preferred == Quality::HiRes)
        if (const auto hit = taggedFirst(QStringLiteral("hires")))
            return hit;
    if (preferred == Quality::Lossless)
        if (const auto hit = losslessFirst())
            return hit;

    for (const auto quality : degradeChain(preferred)) {
        std::optional<AudioStream> hit;
        switch (quality) {
        case Quality::HiRes:
            hit = taggedFirst(QStringLiteral("hires"));
            break;
        case Quality::Lossless:
            hit = losslessFirst();
            if (!hit)
                for (const auto& stream : regular)
                    if (matchesRegularBand(stream, quality))
                        return stream;
            break;
        default:
            for (const auto& stream : regular)
                if (matchesRegularBand(stream, quality))
                    return stream;
            break;
        }
        if (hit)
            return hit;
    }
    return sorted.isEmpty() ? std::nullopt : std::optional<AudioStream>(sorted.first());
}

QStringList prioritize(const QString& primary, const QStringList& backups)
{
    QStringList deduped;
    for (const auto& raw : QStringList(primary) + backups) {
        const auto url = raw.trimmed();
        if (!url.isEmpty() && !deduped.contains(url))
            deduped.append(url);
    }
    std::stable_sort(deduped.begin(), deduped.end(), [](const QString& a, const QString& b) {
        return hostScore(a) > hostScore(b);
    });
    return deduped;
}

QString qualityKeyOf(const AudioStream& stream)
{
    const auto tag = normalizedTag(stream);
    if (!tag.isEmpty())
        return tag;
    if (isLosslessLike(stream))
        return QStringLiteral("lossless");
    if (stream.bitrateKbps >= minBitrateKbps(Quality::High))
        return QStringLiteral("high");
    if (stream.bitrateKbps >= minBitrateKbps(Quality::Medium))
        return QStringLiteral("medium");
    return QStringLiteral("low");
}

} // namespace cirrus::bilibili
