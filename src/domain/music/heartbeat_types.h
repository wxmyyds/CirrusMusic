#pragma once
#include "catalog_types.h"

namespace cirrus::domain
{
// 心动模式 (docs/api/heartbeat-mode.md): a playlist's songs with similar ones put in
// between, in play order, as the official app asks for it when the mode starts. A new mix
// each time it is asked; the caller keeps it as its queue.
struct HeartbeatQuery {
    static constexpr int MaximumCount = 10000; // a saved queue's limit

    PlaylistId playlist;  // the account's liked songs (the caller checks it is)
    SongId anchor;        // the song the mix starts from
    SongId start;         // the list's first song
    int count = 0;        // the list's length from `start`
    bool fromAll = false; // the whole list from its start, rather than from `anchor`

    [[nodiscard]] bool isValid() const
    {
        return validMusicId(playlist.value) && validMusicId(anchor.value) &&
               validMusicId(start.value) && count >= 1 && count <= MaximumCount;
    }
};
struct HeartbeatEntry {
    SongId song;
    // Always for a recommended song; for one of the playlist's own, only when the reply
    // carries it (the caller has those already).
    std::optional<Song> details;
    QString alg; // the provider's recommendation mark, sent back with listening reports
    bool recommended = false; // put in by the mix, not one of the playlist's songs
};
struct HeartbeatList {
    QList<HeartbeatEntry> entries; // play order; each song once
};
} // namespace cirrus::domain
