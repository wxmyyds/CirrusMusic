#include "spoken_mapping.h"
#include "music_mapping.h"
#include <cmath>
namespace cirrus::netease::mapping
{
using namespace domain;
namespace
{
bool absent(const QJsonValue& v) { return v.isUndefined() || v.isNull(); }
bool number(const QJsonValue& v, std::optional<qint64>& out)
{
    if (absent(v))
        return true;
    out = nonnegative(v);
    return out.has_value();
}
} // namespace
std::optional<Podcast> podcastFrom(const QJsonObject& o)
{
    Podcast p;
    p.id = {providerId(o.value(QStringLiteral("id")))};
    const auto title = o.value(QStringLiteral("name")), desc = o.value(QStringLiteral("desc"));
    if (p.id.value.isEmpty() || !textField(title) || !textField(desc) ||
        !number(o.value(QStringLiteral("programCount")), p.voiceCount))
        return std::nullopt;
    p.title = title.toString();
    if (!absent(desc))
        p.description = desc.toString();
    p.cover = imageFrom(o.value(QStringLiteral("picUrl")));
    return p;
}
std::optional<Voice> voiceFrom(const QJsonObject& o)
{
    Voice v;
    v.id = {providerId(o.value(QStringLiteral("id")))};
    const auto title = o.value(QStringLiteral("name"));
    if (v.id.value.isEmpty() || !textField(title) ||
        !number(o.value(QStringLiteral("duration")), v.providerDuration))
        return std::nullopt;
    v.title = title.toString();
    v.cover = imageFrom(o.value(QStringLiteral("coverUrl")));
    const auto radio = o.value(QStringLiteral("radio"));
    if (!absent(radio)) {
        if (!radio.isObject())
            return std::nullopt;
        v.podcast = podcastFrom(radio.toObject());
        if (!v.podcast)
            return std::nullopt;
    }
    v.track = trackFrom(o);
    return v;
}
std::optional<SongId> trackFrom(const QJsonObject& program)
{
    // The app reads mainSong only (never mainTrackId) and plays it as a song.
    const auto song = program.value(QStringLiteral("mainSong"));
    if (!song.isObject())
        return std::nullopt;
    SongId id{providerId(song.toObject().value(QStringLiteral("id")))};
    return validMusicId(id.value) ? std::optional(id) : std::nullopt;
}
std::optional<DiscoveryPlaylist> discoveryPlaylistFrom(const QJsonObject& o)
{
    DiscoveryPlaylist p;
    p.id = {providerId(o.value(QStringLiteral("id")))};
    const auto name = o.value(QStringLiteral("name")),
               desc = o.value(QStringLiteral("description"));
    if (p.id.value.isEmpty() || !textField(name) || !textField(desc) ||
        !number(o.value(QStringLiteral("trackCount")), p.trackCount))
        return std::nullopt;
    p.title = name.toString();
    if (!absent(desc))
        p.summary = desc.toString();
    p.cover = imageFrom(o.value(QStringLiteral("coverImgUrl")));
    const auto plays = o.value(QStringLiteral("playCount"));
    if (!absent(plays)) {
        if (!plays.isDouble() || !std::isfinite(plays.toDouble()) || plays.toDouble() < 0 ||
            plays.toDouble() > double(MaximumPlaylistCursor))
            return std::nullopt;
        p.playCount = plays.toDouble();
    }
    return p;
}
} // namespace cirrus::netease::mapping
