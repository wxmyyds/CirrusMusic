#pragma once

#include "sources.h"

#include <QObject>

#include <memory>

namespace cirrus::application
{
class BackendFacade;
}
namespace cirrus::domain
{
struct DailyHistoryDates;
}
namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

class MusicRequests;

// The read-only catalog from the backend (docs/ui/integration.md §4 CatalogSource),
// one group per step: search (step 4), details with the daily songs (step 5), home
// and discover (step 8).
class BackendCatalog : public QObject, public CatalogSource
{
  public:
    explicit BackendCatalog(runtime::BackendRuntime& runtime, QObject* parent = nullptr);
    ~BackendCatalog() override;

    // Step 4: search.
    Request suggestions(const QString& text, Reply<QList<SearchSuggestion>> reply) override;
    Request hotKeywords(Reply<QList<HotKeyword>> reply) override;
    Request searchSongs(const QString& text, PageRequest page,
                        Reply<ResultPage<SongInfo>> reply) override;
    Request searchCollections(collection::Kind kind, const QString& text, PageRequest page,
                              Reply<ResultPage<CollectionItem>> reply) override;
    Request searchVoices(const QString& text, PageRequest page,
                         Reply<ResultPage<SongInfo>> reply) override;

    // Step 5: details.
    Request playlist(const QString& key, Reply<PlaylistPage> reply) override;
    Request songDetails(const QStringList& keys, Reply<QList<SongInfo>> reply) override;
    Request album(const QString& key, Reply<AlbumPage> reply) override;
    Request artist(const QString& key, Reply<ArtistPage> reply) override;
    Request artistSongs(const QString& key, bool byTime, PageRequest page,
                        Reply<ResultPage<SongInfo>> reply) override;
    Request artistAlbums(const QString& key, PageRequest page,
                         Reply<ResultPage<CollectionItem>> reply) override;
    Request artistIntroduction(const QString& key, Reply<ArtistIntroduction> reply) override;
    Request similarArtists(const QString& key, Reply<QList<CollectionItem>> reply) override;
    Request songWiki(const SongInfo& song, Reply<SongWiki> reply) override;
    Request similarSongs(const QString& key, Reply<QList<SongInfo>> reply) override;
    Request similarPlaylists(const QString& key, Reply<QList<CollectionItem>> reply) override;
    Request podcast(const QString& key, Reply<PodcastInfo> reply) override;
    Request voice(const QString& key, Reply<SongInfo> reply) override;
    Request podcastVoices(const QString& key, bool oldestFirst, PageRequest page,
                          Reply<ResultPage<SongInfo>> reply) override;
    Request dailySongs(Reply<QList<SongInfo>> reply) override;
    Request dailyHistoryDates(Reply<QStringList> reply) override;
    Request dailyHistorySongs(const QString& date, Reply<QList<SongInfo>> reply) override;

    // Step 8: home and discover (backend_discover.cpp).
    // No page uses the feed any more (docs/ui/pages/home.md §8): an empty list.
    Request feed(Feed which, Reply<QList<FeedSection>> reply) override;
    Request shelf(feed::Shelf which, Reply<QList<CollectionItem>> reply) override;
    Request charts(Reply<QList<ChartInfo>> reply) override;
    // Three reads: the catalogue (its groups), the hot and the premium categories; the
    // catalogue alone settles it, the other two are left empty if they fail.
    Request playlistCategories(Reply<PlaylistCategories> reply) override;
    Request categoryPlaylists(const QString& category, bool newest, PageRequest page,
                              Reply<ResultPage<CollectionItem>> reply) override;
    // The next page's `before` is the last playlist's update time (the service's cursor).
    Request premiumPlaylists(const QString& category, qint64 before, int limit,
                             Reply<ResultPage<CollectionItem>> reply) override;
    Request browseArtists(int kind, int area, const QString& initial, PageRequest page,
                          Reply<ResultPage<CollectionItem>> reply) override;
    Request newAlbums(int area, PageRequest page, Reply<ResultPage<CollectionItem>> reply) override;
    // This month's chart, its week or month group.
    Request albumChart(int area, bool monthly, Reply<QList<CollectionItem>> reply) override;

    // Users (backend_users.cpp, docs/api/user-profile-reads.md).
    Request user(const QString& key, Reply<UserInfo> reply) override;
    Request userPlaylists(const QString& key, Reply<QList<CollectionItem>> reply) override;
    Request userRanking(const QString& key, bool allTime, Reply<UserRanking> reply) override;
    Request userFollows(const QString& key, PageRequest page, Reply<UserList> reply) override;
    Request userFans(const QString& key, qint64 before, int limit, Reply<UserList> reply) override;

  private:
    MusicRequests* m_requests;
    // The signed-in account's key (a playlist of its own), empty for guests.
    std::function<QString()> m_session;
    // The last list of past recommendation days: a day's songs are read only for a day
    // the service listed (domain::DailyHistorySongsQuery).
    std::shared_ptr<std::shared_ptr<const domain::DailyHistoryDates>> m_historyDates;
};

} // namespace cirrus::ui
