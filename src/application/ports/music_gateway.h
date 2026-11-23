#pragma once

#include "domain/account/session_types.h"
#include "domain/playback/playback_types.h"
#include "domain/music/account_library_types.h"
#include "domain/music/cache_retention.h"
#include "domain/music/catalog_types.h"
#include "domain/music/chart_types.h"
#include "domain/music/cloud_history_types.h"
#include "domain/music/comment_types.h"
#include "domain/music/discovery_browse_types.h"
#include "domain/music/discovery_types.h"
#include "domain/music/entity_search_types.h"
#include "domain/music/introduction_types.h"
#include "domain/music/library_types.h"
#include "domain/music/playlist_discovery_types.h"
#include "domain/music/podcast_types.h"
#include "domain/music/recent_entity_types.h"
#include "domain/music/recommendation_history_types.h"
#include "domain/music/search_assist_types.h"
#include "domain/music/song_wiki_text_types.h"
#include "domain/music/user_profile_types.h"

#include <QObject>
#include <algorithm>
#include <type_traits>
#include <variant>

namespace cirrus::application
{

enum class MusicError {
    InvalidRequest,
    Busy,
    Canceled,
    StaleContext,
    Network,
    Timeout,
    RateLimited,
    ProviderRejected,
    InvalidResponse,
    ProtocolFailure,
    IncompleteCatalog,
    AuthenticationRequired
};
struct MusicFailure {
    MusicError error{};
    std::optional<int> providerCode;
    std::optional<qint64> retryAfterMs;
};
struct DetailQuery {
    QList<domain::SongId> songs;
    [[nodiscard]] bool isValid() const
    {
        if (songs.isEmpty() || songs.size() > 100)
            return false;
        QStringList seen;
        for (const auto& id : songs) {
            if (!domain::validMusicId(id.value) || seen.contains(id.value))
                return false;
            seen.append(id.value);
        }
        return true;
    }
};
struct LyricQuery {
    domain::SongId song;
    [[nodiscard]] bool isValid() const { return domain::validMusicId(song.value); }
};
struct PlaybackLyricQuery {
    domain::PlaybackSourceSelection source;
    [[nodiscard]] bool isValid() const
    {
        if (!domain::validMusicId(source.song.value) || !source.token.session.isValid() ||
            !source.token.generation)
            return false;
        if (source.provider == domain::SourceProvider::Netease)
            return !source.bili;
        return source.provider == domain::SourceProvider::Bilibili && source.bili &&
               source.bili->bvid.startsWith(QStringLiteral("BV")) &&
               source.bili->bvid.size() <= 32 &&
               std::all_of(source.bili->bvid.cbegin(), source.bili->bvid.cend(), [](QChar c) {
                   return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') ||
                          (c >= u'0' && c <= u'9');
               }) && domain::validMusicId(source.bili->cid);
    }
};
using MusicQuery = std::variant<
    domain::SongSearch, DetailQuery, LyricQuery, PlaybackLyricQuery, domain::AlbumQuery, domain::ArtistQuery,
    domain::ArtistSongsQuery, domain::ArtistAlbumsQuery, domain::PlaylistQuery,
    domain::PlaylistSongsQuery, domain::DailyRecommendationsQuery, domain::PersonalFmQuery,
    domain::MyPlaylistsQuery, domain::LikedSongIdsQuery, domain::SavedAlbumsQuery,
    domain::SavedArtistsQuery, domain::RecentSongsQuery, domain::ListeningRankingQuery,
    domain::ChartsQuery, domain::RecommendedPlaylistsQuery, domain::PlaylistListQuery,
    domain::PremiumPlaylistsQuery, domain::PlaylistTagsQuery, domain::EntitySearchQuery,
    domain::PodcastSearchQuery, domain::VoiceSearchQuery, domain::PodcastDetailQuery,
    domain::PodcastProgramsQuery, domain::ProgramDetailQuery, domain::RecentEntitiesQuery,
    domain::CommentsQuery, domain::HotCommentsQuery, domain::SortedCommentsQuery,
    domain::CommentFloorQuery, domain::CommentInfoQuery, domain::SubscribedPodcastsQuery,
    domain::CloudSongsQuery, domain::CloudSongDetailsQuery, domain::ArtistIntroductionQuery,
    domain::SongWikiQuery, domain::SearchSuggestionsQuery, domain::HotKeywordsQuery,
    domain::HotKeywordDetailsQuery, domain::DailyRecommendedPlaylistsQuery,
    domain::DailyHistoryDatesQuery, domain::DailyHistorySongsQuery, domain::DiscoveryBrowseQuery,
    domain::SongWikiTextQuery, domain::UserProfileQuery>;
using MusicResult = std::variant<
    domain::SongSearchPage, domain::SongDetails, domain::SongLyrics, MusicFailure,
    domain::AlbumDetails, domain::ArtistDetails, domain::ArtistSongsPage, domain::ArtistAlbumsPage,
    domain::PlaylistDetails, domain::PlaylistSongsPage, domain::DailyRecommendations,
    domain::PersonalFmBatch, domain::MyPlaylistsPage, domain::LikedSongIds, domain::SavedAlbumsPage,
    domain::SavedArtistsPage, domain::CloudRecentSongs, domain::ListeningRanking, domain::Charts,
    domain::RecommendedPlaylists, domain::PlaylistListPage, domain::PremiumPlaylistsPage,
    domain::PlaylistTags, domain::AlbumSearchPage, domain::ArtistSearchPage,
    domain::PlaylistSearchPage, domain::PodcastSearchPage, domain::VoiceSearchPage,
    domain::PodcastDetails, domain::PodcastProgramsPage, domain::ProgramDetails,
    domain::CloudRecentPlaylists, domain::CloudRecentAlbums, domain::CloudRecentPodcasts,
    domain::CloudRecentVoices, domain::CommentsPage, domain::HotCommentsPage,
    domain::SortedCommentsPage, domain::CommentFloorPage, domain::CommentStatistics,
    domain::SubscribedPodcastsPage, domain::CloudSongsPage, domain::CloudSongDetails,
    domain::ArtistIntroduction, domain::SongWikiSummary, domain::SearchSuggestions,
    domain::HotKeywords, domain::HotKeywordDetails, domain::DailyRecommendedPlaylists,
    domain::DailyHistoryDates, domain::DailyHistorySongs, domain::DiscoveryBrowsePage,
    domain::SongWikiText, domain::UserProfilePage>;

inline bool validMusicQuery(const MusicQuery& query)
{
    return std::visit([](const auto& value) { return value.isValid(); }, query);
}

inline bool isRecommendationHistoryQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::DailyHistoryDatesQuery>(query) ||
           std::holds_alternative<domain::DailyHistorySongsQuery>(query);
}
inline std::optional<domain::SessionToken> musicQuerySourceContext(const MusicQuery& query)
{
    if (const auto* history = std::get_if<domain::DailyHistorySongsQuery>(&query))
        return history->sourceContext();
    if (const auto* wiki = std::get_if<domain::SongWikiTextQuery>(&query))
        return wiki->sourceContext();
    return std::nullopt;
}

inline bool isSearchAssistQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::SearchSuggestionsQuery>(query) ||
           std::holds_alternative<domain::HotKeywordsQuery>(query) ||
           std::holds_alternative<domain::HotKeywordDetailsQuery>(query);
}

inline bool isAccountLibraryQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::SubscribedPodcastsQuery>(query) ||
           std::holds_alternative<domain::CloudSongsQuery>(query) ||
           std::holds_alternative<domain::CloudSongDetailsQuery>(query);
}

inline bool isCommentQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::CommentsQuery>(query) ||
           std::holds_alternative<domain::HotCommentsQuery>(query) ||
           std::holds_alternative<domain::SortedCommentsQuery>(query) ||
           std::holds_alternative<domain::CommentFloorQuery>(query) ||
           std::holds_alternative<domain::CommentInfoQuery>(query);
}

inline bool isPodcastQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::PodcastDetailQuery>(query) ||
           std::holds_alternative<domain::PodcastProgramsQuery>(query) ||
           std::holds_alternative<domain::ProgramDetailQuery>(query);
}

inline bool isEntitySearchQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::EntitySearchQuery>(query) ||
           std::holds_alternative<domain::PodcastSearchQuery>(query) ||
           std::holds_alternative<domain::VoiceSearchQuery>(query);
}

// Daily playlist supply shares mapping, not the public recommendation's authentication policy.
inline bool isPlaylistDiscoveryQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::DailyRecommendedPlaylistsQuery>(query) ||
           std::holds_alternative<domain::RecommendedPlaylistsQuery>(query) ||
           std::holds_alternative<domain::PlaylistListQuery>(query) ||
           std::holds_alternative<domain::PremiumPlaylistsQuery>(query) ||
           std::holds_alternative<domain::PlaylistTagsQuery>(query);
}

inline std::optional<QString> libraryQueryAccount(const MusicQuery& query)
{
    return std::visit(
        [](const auto& value) -> std::optional<QString> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, domain::MyPlaylistsQuery> ||
                          std::is_same_v<T, domain::LikedSongIdsQuery> ||
                          std::is_same_v<T, domain::SavedAlbumsQuery> ||
                          std::is_same_v<T, domain::SavedArtistsQuery> ||
                          std::is_same_v<T, domain::SubscribedPodcastsQuery> ||
                          std::is_same_v<T, domain::CloudSongsQuery> ||
                          std::is_same_v<T, domain::CloudSongDetailsQuery> ||
                          std::is_same_v<T, domain::DailyRecommendedPlaylistsQuery>)
                return value.accountId;
            return std::nullopt;
        },
        query);
}
inline std::optional<QString> historyQueryAccount(const MusicQuery& query)
{
    return std::visit(
        [](const auto& value) -> std::optional<QString> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, domain::RecentSongsQuery> ||
                          std::is_same_v<T, domain::ListeningRankingQuery> ||
                          std::is_same_v<T, domain::RecentEntitiesQuery> ||
                          std::is_same_v<T, domain::DailyHistoryDatesQuery>)
                return value.accountId;
            if constexpr (std::is_same_v<T, domain::DailyHistorySongsQuery>)
                return value.accountId();
            return std::nullopt;
        },
        query);
}
inline std::optional<QString> musicQueryAccount(const MusicQuery& query)
{
    if (const auto account = libraryQueryAccount(query))
        return account;
    return historyQueryAccount(query);
}
inline bool requiresAuthenticatedMusicQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::DailyRecommendationsQuery>(query) ||
           std::holds_alternative<domain::PersonalFmQuery>(query) ||
           musicQueryAccount(query).has_value();
}

// Same-thread, deferred, value-only port; no arbitrary endpoints, JSON or Cookie.
class IMusicGateway : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    [[nodiscard]] virtual quint64 request(domain::SessionToken context, MusicQuery query) = 0;
    virtual void cancel(quint64 id) = 0;
  signals:
    void completed(quint64 id, cirrus::domain::SessionToken context,
                   cirrus::application::MusicResult result,
                   cirrus::domain::CacheRetention retention = {});
};

} // namespace cirrus::application
Q_DECLARE_METATYPE(cirrus::application::MusicResult)
Q_DECLARE_METATYPE(cirrus::application::MusicFailure)
