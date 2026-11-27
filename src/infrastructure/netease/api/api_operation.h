#pragma once

#include "infrastructure/netease/protocol/official_route.h"
#include "infrastructure/netease/protocol/response_decoder.h"

namespace cirrus::netease
{

// Closed native registry. No string-based forwarding from application/QML.
// AnonymousRegister (xeapi): username is derived from the client's device ID;
// its MUSIC_A cookie is NOT staged until the anonymous credential scope exists.
enum class ApiOperation {
    QrKey,
    QrCheck,
    AccountIdentity,
    AnonymousRegister,
    SearchSongs,
    SongDetails,
    SongLyrics,
    SongStream,
    AlbumDetails,
    ArtistDetails,
    ArtistSongs,
    ArtistAlbums,
    PlaylistDetails,
    DailyRecommendations,
    PersonalFm,
    MyPlaylists,
    LikedSongIds,
    SavedAlbums,
    SavedArtists,
    ListeningStart,
    ListeningPlay,
    ListeningActive,
    RecentSongs,
    ListeningRanking,
    Charts,
    ChartsWithPreviews,
    RecommendedPlaylists,
    PlaylistList,
    PremiumPlaylists,
    PlaylistCatalogue,
    HotPlaylistTags,
    PremiumPlaylistTags,
    SearchAlbums,
    SearchArtists,
    SearchPlaylists,
    SearchPodcasts,
    SearchVoices,
    PodcastDetails,
    PodcastPrograms,
    ProgramDetails,
    RecentPlaylists,
    RecentAlbums,
    RecentPodcasts,
    RecentVoices,
    SongComments,
    AlbumComments,
    PlaylistComments,
    ProgramComments,
    HotComments,
    SortedComments,
    CommentFloor,
    CommentInfo,
    SubscribedPodcasts,
    CloudSongs,
    CloudSongDetails,
    SongHeart,
    PlaylistCollectSongs,
    AlbumSubscribe,
    AlbumUnsubscribe,
    ArtistSubscribe,
    ArtistUnsubscribe,
    PodcastSubscribe,
    PodcastUnsubscribe,
    PlaylistSubscribe,
    PlaylistUnsubscribe,
    PlaylistCreate,
    PlaylistRename,
    PlaylistDescription,
    PlaylistDelete,
    PlaylistSongOrder,
    AccountPlaylistOrder,
    ArtistIntroduction,
    SongWikiSummary,
    PlaylistTagsUpdate,
    PlaylistMakePublic,
    CoverAllocate,
    PlaylistCoverCommit,
    CommentLike,
    CommentUnlike,
    CommentPublish,
    CommentReply,
    CommentDelete,
    SearchSuggestions,
    HotKeywords,
    HotKeywordDetails,
    DailyRecommendedPlaylists,
    DailyHistoryDates,
    DailyHistorySongs,
    TogetherCreate,
    TogetherCheck,
    TogetherJoin,
    TogetherStatus,
    TogetherEndCheck,
    TogetherEnd,
    TogetherHeartbeat,
    TogetherPlay,
    TogetherQueue,
    TogetherReplaceQueue,
    TogetherReconnectInfo,
    TogetherDeviceNotice,
    TogetherSyncNotice,
    LatestAlbums,
    NewAlbums,
    AlbumCharts,
    ArtistBrowse,
    PopularArtists,
    ArtistTopSongs,
    SimilarSongs,
    SimilarArtists,
    SimilarPlaylists,
    SongWikiText,
    SmsCodeSend,
    CellphoneLogin,
    EmailLogin,
    UserDetail,
    UserPlaylists,
    UserFollows,
    UserFans,
    UserRanking,
    UserFollow,
    UserUnfollow,
    // 心动模式 (docs/api/heartbeat-mode.md).
    HeartbeatList
};

struct ApiOperationContract {
    QString logicalPath;
    Protocol protocol = Protocol::Eapi;
    ResponseEncoding responseEncoding = ResponseEncoding::Json;
    qsizetype responseLimit = 64 * 1024;
    OfficialDestination destination = OfficialDestination::Default;
};

[[nodiscard]] std::optional<ApiOperationContract>
operationContract(ApiOperation operation, const QJsonObject& parameters = {});
[[nodiscard]] bool requiresAccountCookie(ApiOperation operation);
// Reads whose request may be sent again when its connection closed before any response
// (TransportRequest::retryLostConnection). Listed one by one: writes never are, nor
// reads that make or move something (anonymous registration, personal FM).
[[nodiscard]] bool retriesLostConnection(ApiOperation operation);
[[nodiscard]] bool validQrKey(QStringView key);
[[nodiscard]] std::optional<QJsonObject> operationPayload(ApiOperation operation,
                                                          const QJsonObject& parameters);
// Only recognized operation outcomes may stage response cookies. Not proof of
// account identity; in particular QR 803 still requires identity verification.
[[nodiscard]] bool acceptsResponseCookies(ApiOperation operation, const DecodedResponse& response);

} // namespace cirrus::netease
