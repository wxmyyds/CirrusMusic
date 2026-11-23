#pragma once
#include "bili_models.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace cirrus::bilibili
{

// The song metadata matching needs, independent of the rest of the app: a title, an artist
// (the first credited one), and its duration when the provider knew it (0 otherwise).
struct SongTargetInfo {
    QString title;
    QString artist;
    qint64 durationMs = 0;
    friend bool operator==(const SongTargetInfo&, const SongTargetInfo&) = default;
};

// The search queries for a song, in order: "title artist", "artist title", then title.
// Whitespace-collapsed, blanks and duplicates dropped. Mirrors NeriPlayer's builder.
QStringList buildQueries(const SongTargetInfo& song);

// The provider's duration bucket for the search filter: 0 all, 1 <10m, 2 10–30m, 3 30–60m,
// 4 >60m.
int durationFilter(qint64 durationMs);

// A candidate's score. Title hit 55 (else token overlap 35/18), artist hit 25, duration
// likeness up to 30; a candidate longer than twice the song is −15. Range is not clamped.
int score(const SongTargetInfo& song, const QString& title, const QString& author,
          int durationSec);

// The matching page of a video: `score` is the candidate's own plus the page's title/author/
// duration score; the highest wins. Null when the video has no usable page.
struct PageMatch {
    VideoPage page;
    int score = 0;
};
std::optional<PageMatch> bestPage(const SongTargetInfo& song, const SearchItem& item,
                                  const VideoInfo& video);

// The score below which a candidate is not considered a match.
inline constexpr int kMinimumScore = 70;

} // namespace cirrus::bilibili
