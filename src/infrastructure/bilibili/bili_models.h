#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

namespace cirrus::bilibili
{

// One stream the playurl answer offered: the address(es) for the same logical stream and
// its provider metadata. `url` is the best of `candidates`; both are signed and never logged.
// `size`/`md5` are the provider's when it gave them (often only durl does); AudioStore needs
// an exact size, so a missing one is learned by a Range probe before the source is handed on.
struct AudioStream {
    int id = 0;
    QString mimeType;
    int bitrateKbps = 0;
    QString qualityTag; // "hires" or empty (regular AAC)
    QString codecs;
    QString url;
    QStringList candidates;
    qint64 size = 0;
    QByteArray md5; // 32 lowercase hex when the provider supplied one; never trusted blindly

    friend bool operator==(const AudioStream&, const AudioStream&) = default;
};

// The playurl answer, already mapped: the regular DASH tracks and FLAC/Hi-Res
// separately (merged into `streams`), plus the progressive mp4 fallback's one track.
// `videoAvailable`/`durationMs` only inform matching and the empty-audio retry.
struct PlayUrlResult {
    QList<AudioStream> streams;
    bool videoAvailable = false;
    qint64 durationMs = 0;
};

// The video's own identity and pages, from the view endpoint.
struct VideoPage {
    qint64 cid = 0;
    int index = 0;
    QString part;
    int durationSec = 0;
};

struct VideoInfo {
    qint64 aid = 0;
    QString bvid;
    QString title;
    QString ownerName;
    qint64 durationSec = 0;
    QList<VideoPage> pages;
};

// One search result row.
struct SearchItem {
    qint64 aid = 0;
    QString bvid;
    QString title;
    QString author;
    int durationSec = 0;
    qint64 play = 0;
    friend bool operator==(const SearchItem&, const SearchItem&) = default;
};

} // namespace cirrus::bilibili
