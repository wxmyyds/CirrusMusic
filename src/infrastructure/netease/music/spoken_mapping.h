#pragma once
#include "domain/music/entity_search_types.h"
#include <QJsonObject>
namespace cirrus::netease::mapping
{
std::optional<domain::Podcast> podcastFrom(const QJsonObject& object);
std::optional<domain::Voice> voiceFrom(const QJsonObject& object);
// A program's (or voice's) `mainSong.id`, the song the official app plays for it; none when
// it is missing or not an id.
std::optional<domain::SongId> trackFrom(const QJsonObject& program);
std::optional<domain::DiscoveryPlaylist> discoveryPlaylistFrom(const QJsonObject& object);
} // namespace cirrus::netease::mapping
