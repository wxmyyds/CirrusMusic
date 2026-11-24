#pragma once
#include "domain/music/podcast_types.h"
#include "domain/playback/playback_types.h"
#include "logging/log.h"

namespace cirrus::domain
{
// Where a queued song was picked from, kept with the queue and each listening record for
// local statistics (docs/api/local-listening-history.md §6). Values are stored: append only.
// Personal FM is ListeningMode::PersonalFm, not an origin.
enum class PlayOriginKind {
    None,       // not given (older queues, other callers)
    Playlist,   // id: playlist (liked songs and charts are playlists)
    Album,      // id: album
    Artist,     // id: artist (their top songs)
    DailySongs, // today's recommended songs; no id
    Search,     // search results; no id, never the keywords
    Recent,     // recently played; no id
    Ranking,    // a user's play ranking; id: that user
    Other,
    Podcast // id: the podcast its programs (episodes) are from
};
CIRRUS_LOG_ENUM(PlayOriginKind, None, Playlist, Album, Artist, DailySongs, Search, Recent, Ranking,
                Other, Podcast)
struct PlayOrigin {
    PlayOriginKind kind = PlayOriginKind::None;
    QString id;   // provider id for Playlist/Album/Artist/Ranking, else empty
    QString name; // its title when queued (shown in statistics), <=256 characters
    friend bool operator==(const PlayOrigin&, const PlayOrigin&) = default;
};
inline bool validPlayOrigin(const PlayOrigin& origin)
{
    const bool needsId =
        origin.kind == PlayOriginKind::Playlist || origin.kind == PlayOriginKind::Album ||
        origin.kind == PlayOriginKind::Artist || origin.kind == PlayOriginKind::Ranking ||
        origin.kind == PlayOriginKind::Podcast;
    return origin.kind >= PlayOriginKind::None && origin.kind <= PlayOriginKind::Podcast &&
           (needsId ? validMusicId(origin.id) : origin.id.isEmpty()) &&
           (origin.kind != PlayOriginKind::None || origin.name.isEmpty()) &&
           origin.name.size() <= 256 && origin.name.isValidUtf16() &&
           !origin.name.contains(QChar::Null);
}
struct QueueEntry {
    QUuid id;
    SongId song; // what plays: for a program, its track (mainSong), played as a song
    PlayOrigin origin{};
    // A podcast program (episode) instead of a song, empty for songs: shown, recorded and
    // reported as the program (docs/api/podcast-playback.md).
    ProgramId program{};
};
inline bool validQueueProgram(const ProgramId& program)
{
    return program.value.isEmpty() || validMusicId(program.value);
}
enum class ListeningMode { Queue, PersonalFm };
enum class QueueOrder { Sequential, Shuffle };
enum class QueueRepeat { Off, All, One };
CIRRUS_LOG_ENUM(ListeningMode, Queue, PersonalFm)
CIRRUS_LOG_ENUM(QueueOrder, Sequential, Shuffle)
CIRRUS_LOG_ENUM(QueueRepeat, Off, All, One)
struct QueueSnapshot {
    QList<QueueEntry> entries;
    QUuid active;
    QueueOrder order{};
    QueueRepeat repeat{};
};
struct ActiveLyricWord {
    qsizetype line = 0;
    qsizetype word = 0;
    friend bool operator==(const ActiveLyricWord&, const ActiveLyricWord&) = default;
};
struct LyricCursor {
    QList<qsizetype> lines;
    QList<ActiveLyricWord> words;
    friend bool operator==(const LyricCursor&, const LyricCursor&) = default;
};
struct LyricVersionCursors {
    LyricCursor standard, enhanced;
    friend bool operator==(const LyricVersionCursors&, const LyricVersionCursors&) = default;
};
struct LyricCursors {
    LyricVersionCursors original, translation, romanization;
    friend bool operator==(const LyricCursors&, const LyricCursors&) = default;
};
enum class LyricsState { None, Loading, Ready, Failed };
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::QueueSnapshot)
Q_DECLARE_METATYPE(cirrus::domain::LyricCursors)
Q_DECLARE_METATYPE(cirrus::domain::SongLyrics)
