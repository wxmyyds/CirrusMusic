#pragma once

#include "types.h"

#include <QDate>

#include <array>

// Local listening statistics as the page reads them (docs/ui/pages/stats.md §1): this
// computer's plays of the signed-in account, or the guest's.
namespace cirrus::ui
{

struct StatsQuery {
    qint64 fromMs = 0; // UTC, inclusive; 0: from the first record
    qint64 toMs = 0;   // UTC, exclusive; 0: no end
    enum PeriodSize { Day, Week, Month };
    PeriodSize periodSize = Day; // local calendar, weeks from Monday
    bool byTime = false;         // rankings by time instead of plays
    int limit = 10;              // per ranking, 1..100
};

struct StatsAmount {
    qint64 playedMs = 0;
    int plays = 0; // heard 30 s (or a short song to its end); previews never count
};

struct StatsPeriod {
    QDate start; // its first local day
    StatsAmount amount;
};

struct StatsSong {
    SongInfo song; // details as first heard; only the key when not `known`
    bool known = false;
    StatsAmount amount;
};

// An artist (key empty when the service gave none) or an album.
struct StatsItem {
    QString key;
    QString title;
    QString coverKey;
    StatsAmount amount;
    int songs = 0;
};

struct StatsOrigin {
    // Heartbeat: the songs a 心动模式 mix of the playlist `key` put in.
    enum Kind { Fm, Playlist, Album, Artist, Daily, Search, Recent, Ranking, Other, Heartbeat };
    Kind kind = Other;
    QString key;  // Playlist, Album, Artist, Heartbeat (its playlist); Ranking: its user
    QString name; // as when played; may be empty
    StatsAmount amount;
};

struct ListeningStats {
    StatsAmount total;
    int records = 0;
    int songs = 0;
    int artists = 0;
    int albums = 0;
    int newSongs = 0;                            // first counted play ever in the range
    int days = 0;                                // local days with any record
    QList<StatsPeriod> periods;                  // only periods with records, oldest first
    std::array<StatsAmount, 7 * 24> weekHours{}; // [weekday * 24 + hour], Monday first
    QList<StatsSong> topSongs;
    QList<StatsItem> topArtists;
    QList<StatsItem> topAlbums;
    QList<StatsOrigin> topOrigins;
    // All time, not the range: days in a row with a play, ending today or yesterday.
    int streak = 0;
    int longestStreak = 0;
    qint64 firstRecordAtMs = -1; // all time; -1 without records
    int withoutDetails = 0;      // songs in the range whose details are still coming
};

} // namespace cirrus::ui
