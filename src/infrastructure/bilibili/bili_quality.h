#pragma once
#include "bili_models.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace cirrus::bilibili
{

// The ordinary audio quality ladder. HiRes is a tag rather than a bitrate band;
// Lossless accepts any FLAC. The auxiliary source does not request Dolby.
enum class Quality { HiRes, Lossless, High, Medium, Low };

// From our own audio-quality setting ("standard"/"exhigh"/"lossless"/"hires"); unknown is High.
Quality qualityFromKey(const QString& key);

// The best stream for `preferred`, degrading down the ladder the provider allows, never up.
// Returns null when `available` is empty. As NeriPlayer: regular tracks first by bitrate,
// then tagged ones; the preferred tag wins if present, else the ladder from it.
std::optional<AudioStream> selectStream(const QList<AudioStream>& available, Quality preferred);

// The CDN candidates of one logical stream, deduplicated and ordered by host preference:
// upos-* on bilivideo first, then bilivideo, then mountaintoys. Never changes the stream.
QStringList prioritize(const QString& primary, const QStringList& backups);

// The quality key for the cache token and logs: "hires"/"lossless"/"high"/"medium"/"low".
QString qualityKeyOf(const AudioStream& stream);

} // namespace cirrus::bilibili
