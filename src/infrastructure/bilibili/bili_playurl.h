#pragma once
#include "bili_models.h"
#include <QJsonObject>
#include <QString>
#include <optional>
namespace cirrus::bilibili
{

// Maps a playurl answer (an already-parsed data object, after the envelope's code checked)
// to the streams we can play: dash.audio first, dolby and flac merged with their tags, and
// the progressive mp4 durl as the last resort (only when it is the single whole-file entry).
// No network, no signing; purely the shape mapping, so it is testable on a fixture.
struct PlayUrlMapping {
    PlayUrlResult result;   // empty streams when the answer offered nothing usable
};

PlayUrlMapping mapPlayUrl(const QJsonObject& data);

// The audio quality keys the providers accept, as playurl's `fnval`. DASH (16) plus Dolby
// (256) so both the regular and Dolby tracks are offered.
inline constexpr int kFnvalDashDolby = (1 << 4) | (1 << 8);

} // namespace cirrus::bilibili
