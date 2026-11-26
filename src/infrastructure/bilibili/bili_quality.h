#pragma once
#include "bili_models.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace cirrus::bilibili
{

// The provider's quality ladder, lowest bitrate threshold per step. `Dolby` and `HiRes` are
// tags rather than bitrate bands; `Lossless` accepts any FLAC.
enum class Quality { Dolby, HiRes, Lossless, High, Medium, Low };

// From our own audio-quality setting ("standard"/"exhigh"/"lossless"/"hires"); unknown is High.
Quality qualityFromKey(const QString& key);

// The best stream for `preferred`, degrading down the ladder the provider allows, never up.
// Returns null when `available` is empty. As NeriPlayer: regular tracks first by bitrate,
// then tagged ones; the preferred tag wins if present, else the ladder from it.
std::optional<AudioStream> selectStream(const QList<AudioStream>& available, Quality preferred);

// The CDN candidates of one logical stream, deduplicated and ordered by host preference:
// upos-* on bilivideo first, then bilivideo, then mountaintoys. Never changes the stream.
QStringList prioritize(const QString& primary, const QStringList& backups);

// The quality key that names a stream (for the cache token and logs): "dolby"/"hires"/
// "lossless"/"high"/"medium"/"low".
QString qualityKeyOf(const AudioStream& stream);

} // namespace cirrus::bilibili
