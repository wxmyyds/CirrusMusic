#pragma once
#include "playlist_discovery_types.h"

namespace cirrus::domain
{
// Parameters shared by search resources, NOT a Song or a playback identity.
struct SearchPage {
    QString keywords;
    int limit = 30, offset = 0;
    [[nodiscard]] bool isValid() const
    {
        return !keywords.trimmed().isEmpty() && keywords.size() <= 256 && keywords.isValidUtf16() &&
               !keywords.contains(QChar::Null) && limit >= 1 && limit <= 100 && offset >= 0 &&
               offset <= 10000;
    }
};
enum class MusicSearchKind { Album, Artist, Playlist };
struct EntitySearchQuery {
    MusicSearchKind kind = MusicSearchKind::Album;
    SearchPage page;
    [[nodiscard]] bool isValid() const
    {
        return page.isValid() &&
               (kind == MusicSearchKind::Album || kind == MusicSearchKind::Artist ||
                kind == MusicSearchKind::Playlist);
    }
};
struct PodcastId {
    QString value;
};
struct VoiceId {
    QString value;
};
struct PodcastSearchQuery {
    SearchPage page{QString{}, 10, 0}; // first page; deliberately not JS offset || 30
    [[nodiscard]] bool isValid() const { return page.isValid(); }
};
struct VoiceSearchQuery {
    SearchPage page;
    [[nodiscard]] bool isValid() const { return page.isValid(); }
};
struct SearchArtist {
    ArtistId id;
    QString name;
    QUrl portrait;
    std::optional<qint64> albumCount, songCount;
};
struct Podcast {
    PodcastId id;
    QString title;
    QUrl cover;
    std::optional<QString> description;
    std::optional<qint64> voiceCount;
};
struct Voice {
    VoiceId id; // provider voice ID, NOT the embedded mainSong.id
    QString title;
    QUrl cover;
    std::optional<Podcast> podcast;
    std::optional<qint64> providerDuration; // raw metadata, not a verified clock/unit contract
    std::optional<SongId> track;            // mainSong.id, as for Program (a voice is a program)
};
struct AlbumSearchPage {
    EntitySearchQuery query;
    QList<Album> albums;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
struct ArtistSearchPage {
    EntitySearchQuery query;
    QList<SearchArtist> artists;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
struct PlaylistSearchPage {
    EntitySearchQuery query;
    QList<DiscoveryPlaylist> playlists;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
struct PodcastSearchPage {
    PodcastSearchQuery query;
    QList<Podcast> podcasts;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
struct VoiceSearchPage {
    VoiceSearchQuery query;
    QList<Voice> voices;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
} // namespace cirrus::domain
