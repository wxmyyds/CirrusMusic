#pragma once

#include "collection_writes.h"
#include "sources.h"

#include <QObject>

namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

class MusicRequests;

// The account's library from the backend (docs/ui/integration.md §4 LibrarySource, step
// 7): its playlists, saved albums, artists and podcasts, liked songs, what the service
// recorded as recently played, the listening ranking, and the writes. Everything needs
// a signed-in account (SignedOut otherwise). Writes go through CollectionWrites.
class BackendLibrary : public QObject, public LibrarySource
{
  public:
    explicit BackendLibrary(runtime::BackendRuntime& runtime, QObject* parent = nullptr);
    ~BackendLibrary() override;

    // The account's playlists only (the rail and the add-to-playlist dialog use them);
    // the other parts come from their own reads.
    Request library(Reply<LibraryPage> reply) override;
    Request recentSongs(int limit, Reply<QList<SongInfo>> reply) override;
    // Not offered: the backend holds back saving others' playlists (§6 D4).
    Request setPlaylistSubscribed(const QString& key, bool subscribed, Reply<bool> reply) override;
    Request setAlbumSubscribed(const QString& key, bool subscribed, Reply<bool> reply) override;
    Request setPodcastSubscribed(const QString& key, bool subscribed, Reply<bool> reply) override;
    Request setArtistFollowed(const QString& key, bool followed, Reply<bool> reply) override;
    Request setUserFollowed(const QString& key, bool followed, Reply<bool> reply) override;
    Request likedSongKeys(Reply<QStringList> reply) override;
    Request likedPlaylist(Reply<CollectionItem> reply) override;
    Request libraryCollections(collection::Kind kind, PageRequest page,
                               Reply<ResultPage<CollectionItem>> reply) override;
    Request recentCollections(collection::Kind kind, int limit,
                              Reply<QList<CollectionItem>> reply) override;
    Request recentVoices(int limit, Reply<QList<SongInfo>> reply) override;
    Request listeningRanking(bool allTime, Reply<QList<SongInfo>> reply) override;
    Request createPlaylist(const QString& title, Reply<QString> reply) override;
    Request setSongLiked(const QString& key, bool liked, Reply<bool> reply) override;
    // In batches of 100 (the service's most), in order; stops at the first that fails.
    Request addToPlaylist(const QString& playlistKey, const QStringList& songKeys,
                          Reply<bool> reply) override;
    Request removeFromPlaylist(const QString& playlistKey, const QStringList& songKeys,
                               Reply<bool> reply) override;
    // Reads the playlist first: the order must be of the songs it has now.
    Request reorderPlaylist(const QString& playlistKey, const QStringList& songKeys,
                            Reply<bool> reply) override;
    // Two writes: the title, then the description.
    Request updatePlaylist(const QString& playlistKey, const QString& title,
                           const QString& description, Reply<bool> reply) override;
    Request deletePlaylist(const QString& playlistKey, Reply<bool> reply) override;
    [[nodiscard]] bool canSavePlaylists() const override { return true; }

  private:
    // Replies `value` once every step was acknowledged.
    Request write(QList<CollectionWrites::Step> steps, Reply<bool> reply, bool value = true);

    MusicRequests* m_requests;
    CollectionWrites* m_writes;
};

} // namespace cirrus::ui
