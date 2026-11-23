#pragma once

#include "account.h"
#include "playback.h"
#include "request.h"
#include "stats_types.h"
#include "types.h"

#include <QByteArray>

#include <functional>
#include <memory>

// Data source interfaces: the only way ViewModels reach data. Fakes (fake_*.cpp) serve
// the sample catalog; the backend implementations arrive in UI phase 5
// (docs/ui/presentation.md §6, §8). Methods are added as pages need them.
namespace cirrus::ui
{

class FakeControls;
class TogetherSource;

// Read-only catalog: home, search, playlist/album/artist details, daily songs.
class CatalogSource
{
  public:
    virtual ~CatalogSource() = default;
    virtual Request playlist(const QString& key, Reply<PlaylistPage> reply) = 0;
    // Details of songs known by key (a large playlist's rows), at most
    // kSongDetailsBatch keys a request. Songs the service has no details for are left
    // out of the reply.
    virtual Request songDetails(const QStringList& keys, Reply<QList<SongInfo>> reply) = 0;
    virtual Request album(const QString& key, Reply<AlbumPage> reply) = 0;
    virtual Request artist(const QString& key, Reply<ArtistPage> reply) = 0;
    // An artist's songs, most popular (`byTime` false) or newest first.
    virtual Request artistSongs(const QString& key, bool byTime, PageRequest page,
                                Reply<ResultPage<SongInfo>> reply) = 0;
    virtual Request artistAlbums(const QString& key, PageRequest page,
                                 Reply<ResultPage<CollectionItem>> reply) = 0;
    virtual Request artistIntroduction(const QString& key, Reply<ArtistIntroduction> reply) = 0;
    // The service may want a signed-in account.
    virtual Request similarArtists(const QString& key, Reply<QList<CollectionItem>> reply) = 0;
    // The song page (docs/ui/pages/song.md): the song's wiki (both of the service's wiki
    // reads; a part that fails is left out unless both do), and songs and playlists like
    // it. The wiki needs the song as read (its title and album).
    virtual Request songWiki(const SongInfo& song, Reply<SongWiki> reply) = 0;
    virtual Request similarSongs(const QString& key, Reply<QList<SongInfo>> reply) = 0;
    virtual Request similarPlaylists(const QString& key, Reply<QList<CollectionItem>> reply) = 0;

    // Search: completions for what is typed, trending keywords, and paged results.
    virtual Request suggestions(const QString& text, Reply<QList<SearchSuggestion>> reply) = 0;
    virtual Request hotKeywords(Reply<QList<HotKeyword>> reply) = 0;
    virtual Request searchSongs(const QString& text, PageRequest page,
                                Reply<ResultPage<SongInfo>> reply) = 0;
    virtual Request searchCollections(collection::Kind kind, const QString& text, PageRequest page,
                                      Reply<ResultPage<CollectionItem>> reply) = 0;
    // Podcast voices (episodes), as SongInfo with the podcast in albumKey/albumTitle.
    virtual Request searchVoices(const QString& text, PageRequest page,
                                 Reply<ResultPage<SongInfo>> reply) = 0;

    // A podcast and its voices, newest (or oldest) first.
    virtual Request podcast(const QString& key, Reply<PodcastInfo> reply) = 0;
    virtual Request podcastVoices(const QString& key, bool oldestFirst, PageRequest page,
                                  Reply<ResultPage<SongInfo>> reply) = 0;

    // Home or discover sections.
    virtual Request feed(Feed which, Reply<QList<FeedSection>> reply) = 0;
    // Today's recommended songs; signed-in only.
    virtual Request dailySongs(Reply<QList<SongInfo>> reply) = 0;
    // Past days whose recommendations can be read ("yyyy-MM-dd", newest first), and
    // one of those days' songs (without reasons). Signed-in only; the service may
    // refuse either (e.g. for accounts without the membership it wants).
    virtual Request dailyHistoryDates(Reply<QStringList> reply) = 0;
    virtual Request dailyHistorySongs(const QString& date, Reply<QList<SongInfo>> reply) = 0;
    // A row of cards; DailyPlaylists is signed-in only.
    virtual Request shelf(feed::Shelf which, Reply<QList<CollectionItem>> reply) = 0;
    // The charts; some with previews of their top songs.
    virtual Request charts(Reply<QList<ChartInfo>> reply) = 0;

    // Discover (docs/ui/pages/discover.md §3). Categories are names; "" is all.
    virtual Request playlistCategories(Reply<PlaylistCategories> reply) = 0;
    // A category's playlists, most played or newest first.
    virtual Request categoryPlaylists(const QString& category, bool newest, PageRequest page,
                                      Reply<ResultPage<CollectionItem>> reply) = 0;
    // Premium playlists a page at a time: `before` is the previous page's `next` (0 for
    // the first page).
    virtual Request premiumPlaylists(const QString& category, qint64 before, int limit,
                                     Reply<ResultPage<CollectionItem>> reply) = 0;
    // Artists by kind (0 all, 1 male, 2 female, 3 groups), area (0 all, 1 Chinese,
    // 2 Western, 3 Japanese, 4 Korean, 5 other) and initial ("" popular, "A"–"Z", "#").
    virtual Request browseArtists(int kind, int area, const QString& initial, PageRequest page,
                                  Reply<ResultPage<CollectionItem>> reply) = 0;
    // New albums by area (0 all, 1 Chinese, 2 Western, 3 Korean, 4 Japanese), newest
    // first; and the area's new album chart this week or this month.
    virtual Request newAlbums(int area, PageRequest page,
                              Reply<ResultPage<CollectionItem>> reply) = 0;
    virtual Request albumChart(int area, bool monthly, Reply<QList<CollectionItem>> reply) = 0;

    // Users (docs/ui/pages/user.md); guests may read them.
    virtual Request user(const QString& key, Reply<UserInfo> reply) = 0;
    // The playlists the user made and saved, all at once (`created` tells which).
    virtual Request userPlaylists(const QString& key, Reply<QList<CollectionItem>> reply) = 0;
    virtual Request userRanking(const QString& key, bool allTime, Reply<UserRanking> reply) = 0;
    virtual Request userFollows(const QString& key, PageRequest page, Reply<UserList> reply) = 0;
    // Fans, newest first: `before` is the previous page's `next` (0 for the first page).
    virtual Request userFans(const QString& key, qint64 before, int limit,
                             Reply<UserList> reply) = 0;
};

// The signed-in account's library; every request fails with SignedOut for guests.
class LibrarySource
{
  public:
    virtual ~LibrarySource() = default;
    virtual Request library(Reply<LibraryPage> reply) = 0;
    // The latest songs played on any device, newest first.
    virtual Request recentSongs(int limit, Reply<QList<SongInfo>> reply) = 0;
    // Saves or unsaves someone else's playlist; replies with the new state.
    virtual Request setPlaylistSubscribed(const QString& key, bool subscribed,
                                          Reply<bool> reply) = 0;
    // Saves or unsaves an album; replies with the new state.
    virtual Request setAlbumSubscribed(const QString& key, bool subscribed, Reply<bool> reply) = 0;
    // Saves or unsaves a podcast; replies with the new state.
    virtual Request setPodcastSubscribed(const QString& key, bool subscribed,
                                         Reply<bool> reply) = 0;
    // Follows or unfollows an artist; replies with the new state.
    virtual Request setArtistFollowed(const QString& key, bool followed, Reply<bool> reply) = 0;
    // Follows or unfollows a user; replies with the new state.
    virtual Request setUserFollowed(const QString& key, bool followed, Reply<bool> reply) = 0;
    // The songs the account likes, newest first.
    virtual Request likedSongKeys(Reply<QStringList> reply) = 0;
    // The account's "我喜欢的音乐" playlist (a playlist like any other).
    virtual Request likedPlaylist(Reply<CollectionItem> reply) = 0;
    // The library's collections, a page at a time (docs/ui/pages/library.md §2):
    // Playlist the account's own and saved playlists (`created` tells which, when
    // known), Album saved albums, Artist followed artists, Podcast subscribed podcasts.
    virtual Request libraryCollections(collection::Kind kind, PageRequest page,
                                       Reply<ResultPage<CollectionItem>> reply) = 0;
    // Recently played playlists, albums or podcasts (the service's record), newest first.
    virtual Request recentCollections(collection::Kind kind, int limit,
                                      Reply<QList<CollectionItem>> reply) = 0;
    // Recently played podcast voices.
    virtual Request recentVoices(int limit, Reply<QList<SongInfo>> reply) = 0;
    // The songs the account played most, the last week or all time, most first.
    virtual Request listeningRanking(bool allTime, Reply<QList<SongInfo>> reply) = 0;
    // Creates an empty playlist; replies with its key.
    virtual Request createPlaylist(const QString& title, Reply<QString> reply) = 0;

    // Writes; each replies true once saved. Playlist changes are only accepted for
    // the account's own playlists.
    virtual Request setSongLiked(const QString& key, bool liked, Reply<bool> reply) = 0;
    // Adds at the start, skipping songs already in the playlist.
    virtual Request addToPlaylist(const QString& playlistKey, const QStringList& songKeys,
                                  Reply<bool> reply) = 0;
    virtual Request removeFromPlaylist(const QString& playlistKey, const QStringList& songKeys,
                                       Reply<bool> reply) = 0;
    // `songKeys`: every song of the playlist, in the new order.
    virtual Request reorderPlaylist(const QString& playlistKey, const QStringList& songKeys,
                                    Reply<bool> reply) = 0;
    virtual Request updatePlaylist(const QString& playlistKey, const QString& title,
                                   const QString& description, Reply<bool> reply) = 0;
    virtual Request deletePlaylist(const QString& playlistKey, Reply<bool> reply) = 0;
    // Whether setPlaylistSubscribed() is offered.
    [[nodiscard]] virtual bool canSavePlaylists() const = 0;
};

// Comments (docs/ui/pages/comments.md). Reading works for guests; writes need a
// signed-in account and reply true once the service accepted them.
class CommentSource
{
  public:
    virtual ~CommentSource() = default;
    // A page of comments; `cursor` is the previous page's `next` ("" for the first).
    virtual Request comments(const CommentTarget& target, comment::Sort sort, const QString& cursor,
                             int limit, Reply<CommentPage> reply) = 0;
    // The replies under a comment, oldest first, a page at a time like comments().
    virtual Request replies(const CommentTarget& target, const QString& commentKey,
                            const QString& cursor, int limit, Reply<CommentPage> reply) = 0;
    virtual Request setCommentLiked(const CommentTarget& target, const QString& commentKey,
                                    bool liked, Reply<bool> reply) = 0;
    // The service gives no key for a new comment or reply, nor shows it right away
    // (it may be reviewed first).
    virtual Request publishComment(const CommentTarget& target, const QString& text,
                                   Reply<bool> reply) = 0;
    virtual Request replyToComment(const CommentTarget& target, const QString& commentKey,
                                   const QString& text, Reply<bool> reply) = 0;
    // The service decides whether the account may delete it.
    virtual Request deleteComment(const CommentTarget& target, const QString& commentKey,
                                  Reply<bool> reply) = 0;
};

// The resource cache (covers, song details and lyrics) and the song cache (songs
// heard), kept on disk. Clearing them never touches the session, the library or the
// listening history.
class CacheSource
{
  public:
    virtual ~CacheSource() = default;
    virtual Request usage(Reply<CacheUsage> reply) = 0;
    virtual Request clear(Reply<bool> reply) = 0;
    // The song cache: bytes on disk for every account; clearing replies with the usage
    // after it (a song being played goes once it stops).
    virtual Request audioUsage(Reply<qint64> reply) = 0;
    virtual Request clearAudio(Reply<qint64> reply) = 0;
    // Its limit; lowering it removes the songs heard longest ago now.
    virtual void setAudioCapacity(qint64 bytes) = 0;
};

// Local listening statistics (docs/ui/pages/stats.md): the account's plays on this
// computer, or the guest's.
class StatsSource
{
  public:
    virtual ~StatsSource() = default;
    virtual Request stats(const StatsQuery& query, Reply<ListeningStats> reply) = 0;
    // Clears the account's local listening history (never the service's records).
    virtual Request clear(Reply<bool> reply) = 0;
    // Called when what stats() reads changed: a play was recorded, song details arrived,
    // a clear. Possibly often; never synchronously from a command.
    virtual void setChangedHandler(std::function<void()> changed) = 0;
};

// A song's lyrics, all languages and versions.
class LyricsSource
{
  public:
    virtual ~LyricsSource() = default;
    virtual Request lyrics(const QString& songKey, Reply<SongLyrics> reply) = 0;
};

// Account state, QR and phone/email sign-in. Like PlaybackSource: intents, then
// snapshots.
class SessionSource
{
  public:
    virtual ~SessionSource() = default;

    [[nodiscard]] virtual const SessionSnapshot& snapshot() const = 0;
    // Called after the snapshot changed, never synchronously from a command.
    virtual void setChangedHandler(std::function<void()> changed) = 0;

    // `remember` keeps the session across restarts.
    virtual void beginQrLogin(bool remember) = 0;
    virtual void refreshQr() = 0;
    virtual void cancelLogin() = 0;
    virtual void signOut() = 0;
    // Phone/email sign-in. `secret` is the SMS code or the password; a source hashes a
    // password at once and never keeps it. Replaces a QR sign-in in progress.
    virtual void beginFormLogin(account::FormMethod method, const QString& countryCode,
                                const QString& user, const QString& secret, bool remember) = 0;
    // An SMS code for a PhoneCode sign-in; works beside a QR code being shown.
    virtual void sendSmsCode(const QString& countryCode, const QString& phone) = 0;
    virtual void clearFormError() = 0;
};

// Encoded images (JPEG, PNG) by cover key; the cover image provider decodes them.
// `size` is the larger side the image is shown at, in pixels (0 when unknown): the
// source may give a smaller version, or a larger one to scale down.
class ImageSource
{
  public:
    virtual ~ImageSource() = default;
    virtual Request image(const QString& key, int size, Reply<QByteArray> reply) = 0;
};

// Playback and queue commands, and the playback state. Commands are intents: their
// effect shows up in a later snapshot. Personal FM, playNext and append arrive with
// the queue panel (UI phase 4).
class PlaybackSource
{
  public:
    virtual ~PlaybackSource() = default;

    [[nodiscard]] virtual const PlaybackSnapshot& snapshot() const = 0;
    // Called after the snapshot changed, never synchronously from a command.
    virtual void setChangedHandler(std::function<void()> changed) = 0;
    virtual void setNoticeHandler(std::function<void(const QString&)> notice) { Q_UNUSED(notice); }

    // Replaces the queue with `keys` and plays the one at `index`; `origin` is where
    // they come from (the listening reports).
    virtual void playSongs(const QStringList& keys, int index, const PlayOrigin& origin = {}) = 0;
    // The same for podcast voices, when canPlayVoices().
    virtual void playVoices(const QStringList& keys, int index) = 0;
    [[nodiscard]] virtual bool canPlayVoices() const = 0;
    virtual void playPause() = 0;
    virtual void next() = 0;
    // The song before in play order (wrapping with repeat all); Personal FM: back to the
    // start of the song.
    virtual void previous() = 0;
    virtual void seek(qint64 positionMs) = 0;
    // Plays the entry `index` of the snapshot's queue, keeping the queue and its order.
    virtual void playAt(int index) = 0;
    // Edits the queue by places in the snapshot's queue (Queue mode only): removing the
    // playing song goes on with the next one; moving puts the entry at `to`.
    virtual void removeAt(int index) = 0;
    virtual void move(int from, int to) = 0;
    // Adds songs right after the playing one (`next`) or at the end; with nothing in
    // the queue they play. False in Personal FM (its queue can't be edited) or when the
    // queue can't take them.
    virtual bool addSongs(const QStringList& keys, bool next, const PlayOrigin& origin = {}) = 0;
    // Empties the queue and stops (Queue mode).
    virtual void clearQueue() = 0;
    virtual void setVolume(double volume) = 0;
    // Volume normalization (a setting; the fake plays nothing to normalize).
    virtual void setNormalization(bool on) { Q_UNUSED(on); }
    virtual void setBiliSourceEnabled(bool on) { Q_UNUSED(on); }
    [[nodiscard]] virtual BiliAccountState biliAccount() const { return {}; }
    virtual void setBiliAccountHandler(std::function<void()> changed) { Q_UNUSED(changed); }
    virtual void saveBiliCookie(const QString& cookie) { Q_UNUSED(cookie); }
    virtual void clearBiliCookie() {}
    // How one track goes into the next (AppSettings::trackTransition) and the crossfade's
    // length; a setting the fake has no use for either.
    virtual void setTransition(int mode, int crossfadeSeconds)
    {
        Q_UNUSED(mode);
        Q_UNUSED(crossfadeSeconds);
    }
    virtual void setOrder(playback::Order order) = 0;
    virtual void setRepeat(playback::Repeat repeat) = 0;
    // Endless recommendations for the signed-in account (mode PersonalFm).
    virtual void startFm() = 0;
};

struct Sources {
    // Defined where FakeControls is complete (app.cpp).
    Sources();
    Sources(Sources&&) noexcept;
    Sources& operator=(Sources&&) noexcept;
    ~Sources();

    // Delay and failure knobs of the fakes (App.debug); null for real sources.
    // First, so it outlives the fakes that use it.
    std::unique_ptr<FakeControls> fakeControls;
    std::unique_ptr<CatalogSource> catalog;
    std::unique_ptr<ImageSource> images;
    std::unique_ptr<PlaybackSource> playback;
    std::unique_ptr<SessionSource> session;
    std::unique_ptr<LibrarySource> library;
    std::unique_ptr<CommentSource> comments;
    std::unique_ptr<CacheSource> cache;
    std::unique_ptr<LyricsSource> lyrics;
    std::unique_ptr<StatsSource> stats;
    std::unique_ptr<TogetherSource> together; // none in the gallery
};

} // namespace cirrus::ui
