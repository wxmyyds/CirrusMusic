#pragma once
#include "bili_models.h"
#include <QJsonObject>
#include <QString>
#include <optional>
namespace cirrus::bilibili
{

// Maps a playurl answer (an already-parsed data object, after the envelope's code checked)
// to ordinary audio: dash.audio and FLAC/Hi-Res, excluding Dolby, and
// the progressive mp4 durl as the last resort (only when it is the single whole-file entry).
// No network, no signing; purely the shape mapping, so it is testable on a fixture.
struct PlayUrlMapping {
    PlayUrlResult result; // empty streams when the answer offered nothing usable
};

PlayUrlMapping mapPlayUrl(const QJsonObject& data);

// Request ordinary DASH audio; do not request the Dolby audio capability (256).
inline constexpr int kFnvalDash = 1 << 4;

} // namespace cirrus::bilibili
