#pragma once
#include "domain/music/podcast_types.h"
#include "domain/playback/playback_types.h"
#include "logging/log.h"
#include <algorithm>

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
    Podcast,  // id: the podcast its programs (episodes) are from
    Heartbeat // id: the liked-songs playlist whose 心动模式 mix put the song in
              // (docs/tasks/heartbeat-mode.md §7.6); the playlist's own songs stay Playlist
};
CIRRUS_LOG_ENUM(PlayOriginKind, None, Playlist, Album, Artist, DailySongs, Search, Recent, Ranking,
                Other, Podcast, Heartbeat)
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
        origin.kind == PlayOriginKind::Podcast || origin.kind == PlayOriginKind::Heartbeat;
    return origin.kind >= PlayOriginKind::None && origin.kind <= PlayOriginKind::Heartbeat &&
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
    // The provider's recommendation mark a 心动模式 mix gave the song, sent back with its
    // listening reports (docs/tasks/heartbeat-mode.md §7.6); empty otherwise.
    QString alg{};
};
inline bool validQueueProgram(const ProgramId& program)
{
    return program.value.isEmpty() || validMusicId(program.value);
}
// Printable ASCII without spaces, as the provider's marks are; never shown or logged.
inline bool validQueueAlg(const QString& alg)
{
    return alg.size() <= 256 && std::all_of(alg.cbegin(), alg.cend(), [](QChar c) {
               return c.unicode() > 0x20 && c.unicode() < 0x7f;
           });
}
enum class ListeningMode { Queue, PersonalFm };
enum class QueueOrder { Sequential, Shuffle };
enum class QueueRepeat { Off, All, One };
CIRRUS_LOG_ENUM(ListeningMode, Queue, PersonalFm)
CIRRUS_LOG_ENUM(QueueOrder, Sequential, Shuffle)
CIRRUS_LOG_ENUM(QueueRepeat, Off, All, One)
// 心动模式 (docs/tasks/heartbeat-mode.md §7): the queue a heartbeat mix replaced, kept to
// come back to when the mode ends.
struct HeartbeatState {
    PlaylistId playlist; // the liked-songs playlist the mix is from
    QList<QueueEntry> entries;
    QUuid active;
    QueueOrder order{};
    QueueRepeat repeat{};
};
struct QueueSnapshot {
    QList<QueueEntry> entries;
    QUuid active;
    QueueOrder order{};
    QueueRepeat repeat{};
    // In 心动模式 (the entries are its mix, in order, repeated): the queue before it.
    std::optional<HeartbeatState> heartbeat{};
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
