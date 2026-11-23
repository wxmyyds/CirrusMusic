#include "backend_library.h"

#include "backend_mapping.h"
#include "collection_writes.h"
#include "music_requests.h"

#include "application/backend/backend_facade.h"
#include "domain/music/account_library_types.h"
#include "domain/music/cloud_history_types.h"
#include "domain/music/library_types.h"
#include "domain/music/recent_entity_types.h"
#include "infrastructure/runtime/backend_runtime.h"

#include <QSet>

namespace cirrus::ui
{
namespace
{

using namespace mapping;

// The service's most per page and per playlist write.
constexpr int kPage = 100;
// The rail lists at most this many of the account's playlists.
constexpr int kMostPlaylists = 1000;

std::optional<domain::CatalogPage> catalogPage(PageRequest page)
{
    const domain::CatalogPage query{std::clamp(page.limit, 1, kPage), page.offset};
    if (!query.isValid())
        return std::nullopt;
    return query;
}

// The account's own playlist (true), one it saved (false), or not known.
std::optional<bool> ownOf(const domain::LibraryPlaylist& playlist, const QString& account)
{
    if (playlist.accountLikedSongs == true)
        return true;
    if (playlist.creatorId)
        return *playlist.creatorId == account;
    if (playlist.subscribed)
        return !*playlist.subscribed;
    return std::nullopt;
}

CollectionItem libraryPlaylistOf(const domain::LibraryPlaylist& playlist, const QString& account)
{
    CollectionItem item;
    item.key = playlist.id.value;
    item.kind = collection::Playlist;
    item.title = playlist.title;
    item.count = playlist.trackCount ? int(*playlist.trackCount) : -1;
    item.coverKey = coverKey(playlist.cover);
    item.created = ownOf(playlist, account);
    return item;
}

CollectionItem savedArtistOf(const domain::SavedArtist& artist)
{
    CollectionItem item;
    item.key = artist.id.value;
    item.kind = collection::Artist;
    item.title = artist.name;
    item.count = artist.albumCount ? int(*artist.albumCount) : -1;
    item.coverKey = coverKey(artist.portrait);
    return item;
}

ResultPage<CollectionItem> playlistsPageOf(const domain::MyPlaylistsPage& found)
{
    auto page = pageOf<domain::MyPlaylistsPage, CollectionItem>(
        found, found.playlists, [account = found.query.accountId](const auto& playlist) {
            return libraryPlaylistOf(playlist, account);
        });
    page.next = found.nextOffset;
    return page;
}

template <class T> QList<T> entitiesOf(const auto& recent, auto map)
{
    QList<T> items;
    items.reserve(recent.items.size());
    for (const auto& item : recent.items)
        items.append(map(item.entity));
    return items;
}

// Every playlist of the account, a page at a time, up to kMostPlaylists.
struct PlaylistsRead {
    QList<CollectionItem> items;
    QSet<QString> keys;
    // The service's offset, not items.size(): its first page has rows it does not count.
    int offset = 0;
    Request current;
};

void readPlaylists(QPointer<MusicRequests> requests, std::shared_ptr<PlaylistsRead> state,
                   Reply<QList<CollectionItem>> reply)
{
    if (!requests)
        return;
    const int offset = state->offset;
    state->current = read<domain::MyPlaylistsPage>(
        requests.data(),
        [offset](BackendFacade& facade) {
            return signedIn(facade) ? facade.myPlaylists({kPage, offset})
                                    : MusicRequests::NeedsSignIn;
        },
        Reply<ResultPage<CollectionItem>>([requests, weak = std::weak_ptr<PlaylistsRead>(state),
                                           reply](Outcome<ResultPage<CollectionItem>> outcome) {
            const auto state = weak.lock();
            if (!state)
                return;
            if (!outcome.value) {
                // A later page failing still shows the ones read.
                if (state->items.isEmpty())
                    reply({std::nullopt, false, outcome.failure});
                else
                    reply({state->items, true, std::nullopt});
                return;
            }
            const auto& page = *outcome.value;
            // A page's rows may repeat the first page's extra ones; each playlist once.
            for (const auto& item : page.items) {
                if (!state->keys.contains(item.key)) {
                    state->keys.insert(item.key);
                    state->items.append(item);
                }
            }
            const int next = int(page.next.value_or(state->offset + kPage));
            const bool advanced = next > state->offset;
            state->offset = next;
            const bool more = advanced && page.hasMore.value_or(page.items.size() >= kPage) &&
                              !page.items.isEmpty() && state->items.size() < kMostPlaylists &&
                              state->offset < kMostPlaylists;
            if (more)
                readPlaylists(requests, state, reply);
            else
                reply({state->items, false, std::nullopt});
        }),
        playlistsPageOf);
}

} // namespace

BackendLibrary::BackendLibrary(runtime::BackendRuntime& runtime, QObject* parent)
    : QObject(parent), m_requests(new MusicRequests(runtime.facade(), this)),
      m_writes(new CollectionWrites(runtime.facade(), this))
{
}

BackendLibrary::~BackendLibrary() = default;

Request BackendLibrary::library(Reply<LibraryPage> reply)
{
    auto state = std::make_shared<PlaylistsRead>();
    readPlaylists(m_requests, state,
                  [weak = std::weak_ptr<PlaylistsRead>(state),
                   reply = std::move(reply)](Outcome<QList<CollectionItem>> outcome) {
                      if (!outcome.value) {
                          reply({std::nullopt, false, outcome.failure});
                          return;
                      }
                      LibraryPage page;
                      for (const CollectionItem& item : std::as_const(*outcome.value)) {
                          // Not known whose: not offered as one to add songs to.
                          if (item.created == true)
                              page.createdPlaylists << item;
                          else
                              page.savedPlaylists << item;
                      }
                      reply({page, outcome.partial, std::nullopt});
                  });
    return Request([state] { state->current.cancel(); });
}

Request BackendLibrary::recentSongs(int limit, Reply<QList<SongInfo>> reply)
{
    return readWithCovers<domain::CloudRecentSongs>(
        m_requests,
        [limit = std::clamp(limit, 1, 100)](BackendFacade& facade) {
            return signedIn(facade) ? facade.recentSongs(limit) : MusicRequests::NeedsSignIn;
        },
        std::move(reply),
        [](const domain::CloudRecentSongs& found) {
            QList<SongInfo> songs;
            songs.reserve(found.songs.size());
            for (const auto& recent : found.songs)
                songs.append(songOf(recent.song));
            return songs;
        },
        listSongs);
}

Request BackendLibrary::setPlaylistSubscribed(const QString& key, bool subscribed,
                                              Reply<bool> reply)
{
    const domain::PlaylistSubscription change{domain::PlaylistId{key},
                                              subscribed ? domain::CollectionAction::Collect
                                                         : domain::CollectionAction::Remove};
    return write({[change](BackendFacade&) { return domain::CollectionChange(change); }},
                 std::move(reply), subscribed);
}

Request BackendLibrary::setAlbumSubscribed(const QString& key, bool subscribed, Reply<bool> reply)
{
    const domain::AlbumCollection change{domain::AlbumId{key},
                                         subscribed ? domain::CollectionAction::Collect
                                                    : domain::CollectionAction::Remove};
    return write({[change](BackendFacade&) { return domain::CollectionChange(change); }},
                 std::move(reply), subscribed);
}

Request BackendLibrary::setPodcastSubscribed(const QString& key, bool subscribed, Reply<bool> reply)
{
    const domain::PodcastSubscription change{domain::PodcastId{key},
                                             subscribed ? domain::CollectionAction::Collect
                                                        : domain::CollectionAction::Remove};
    return write({[change](BackendFacade&) { return domain::CollectionChange(change); }},
                 std::move(reply), subscribed);
}

Request BackendLibrary::setArtistFollowed(const QString& key, bool followed, Reply<bool> reply)
{
    const domain::ArtistCollection change{domain::ArtistId{key},
                                          followed ? domain::CollectionAction::Collect
                                                   : domain::CollectionAction::Remove};
    return write({[change](BackendFacade&) { return domain::CollectionChange(change); }},
                 std::move(reply), followed);
}

Request BackendLibrary::setUserFollowed(const QString& key, bool followed, Reply<bool> reply)
{
    const domain::UserFollow change{key, followed ? domain::CollectionAction::Collect
                                                  : domain::CollectionAction::Remove};
    return write({[change](BackendFacade&) { return domain::CollectionChange(change); }},
                 std::move(reply), followed);
}

Request BackendLibrary::likedSongKeys(Reply<QStringList> reply)
{
    return read<domain::LikedSongIds>(
        m_requests,
        [](BackendFacade& facade) {
            return signedIn(facade) ? facade.likedSongIds() : MusicRequests::NeedsSignIn;
        },
        std::move(reply),
        [](const domain::LikedSongIds& found) {
            QStringList keys;
            keys.reserve(found.ids.size());
            for (const auto& id : found.ids)
                keys.append(id.value);
            return keys;
        });
}

Request BackendLibrary::likedPlaylist(Reply<CollectionItem> reply)
{
    // The service lists it among the first; marked, never told by its position.
    return m_requests->submit(
        [](BackendFacade& facade) {
            return signedIn(facade) ? facade.myPlaylists({kPage, 0}) : MusicRequests::NeedsSignIn;
        },
        [reply](const application::MusicResult& result) {
            if (const auto* found = std::get_if<domain::MyPlaylistsPage>(&result)) {
                for (const auto& playlist : found->playlists) {
                    if (playlist.accountLikedSongs == true) {
                        reply({libraryPlaylistOf(playlist, found->query.accountId), false,
                               std::nullopt});
                        return;
                    }
                }
            }
            reply({std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
        },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply({std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

Request BackendLibrary::libraryCollections(collection::Kind kind, PageRequest page,
                                           Reply<ResultPage<CollectionItem>> reply)
{
    const auto query = catalogPage(page);
    if (!query)
        return replyLater(
            this, std::move(reply),
            Outcome<ResultPage<CollectionItem>>{ResultPage<CollectionItem>{{}, false, {}, {}}});
    switch (kind) {
    case collection::Playlist: {
        // All of them on the first page, read as the rail does: the service sends its
        // whole list whatever the limit (a limit of 30 and of 100 got the same reply,
        // 2026-11-09), and whether it keeps to the offset is not known.
        if (page.offset > 0)
            return replyLater(
                this, std::move(reply),
                Outcome<ResultPage<CollectionItem>>{ResultPage<CollectionItem>{{}, false, {}, {}}});
        auto state = std::make_shared<PlaylistsRead>();
        readPlaylists(
            m_requests, state, [reply = std::move(reply)](Outcome<QList<CollectionItem>> outcome) {
                if (!outcome.value) {
                    reply({std::nullopt, false, outcome.failure});
                    return;
                }
                const auto total = qint64(outcome.value->size());
                reply({ResultPage<CollectionItem>{std::move(*outcome.value), false, total, {}},
                       outcome.partial, std::nullopt});
            });
        return Request([state] { state->current.cancel(); });
    }
    case collection::Album:
        return read<domain::SavedAlbumsPage>(
            m_requests,
            [query = *query](BackendFacade& facade) {
                return signedIn(facade) ? facade.savedAlbums(query) : MusicRequests::NeedsSignIn;
            },
            std::move(reply),
            [](const domain::SavedAlbumsPage& found) {
                return pageOf<domain::SavedAlbumsPage, CollectionItem>(found, found.albums,
                                                                       albumOf);
            });
    case collection::Artist:
        return read<domain::SavedArtistsPage>(
            m_requests,
            [query = *query](BackendFacade& facade) {
                return signedIn(facade) ? facade.savedArtists(query) : MusicRequests::NeedsSignIn;
            },
            std::move(reply),
            [](const domain::SavedArtistsPage& found) {
                return pageOf<domain::SavedArtistsPage, CollectionItem>(found, found.artists,
                                                                        savedArtistOf);
            });
    case collection::Podcast:
        return read<domain::SubscribedPodcastsPage>(
            m_requests,
            [query = *query](BackendFacade& facade) {
                return signedIn(facade) ? facade.subscribedPodcasts(query)
                                        : MusicRequests::NeedsSignIn;
            },
            std::move(reply),
            [](const domain::SubscribedPodcastsPage& found) {
                return pageOf<domain::SubscribedPodcastsPage, CollectionItem>(found, found.podcasts,
                                                                              podcastOf);
            });
    }
    return replyLater(this, std::move(reply),
                      Outcome<ResultPage<CollectionItem>>{std::nullopt, false,
                                                          Failure{failure::Invalid, std::nullopt}});
}

Request BackendLibrary::recentCollections(collection::Kind kind, int limit,
                                          Reply<QList<CollectionItem>> reply)
{
    limit = std::clamp(limit, 1, 100);
    auto send = [kind, limit](BackendFacade& facade) -> quint64 {
        if (!signedIn(facade))
            return MusicRequests::NeedsSignIn;
        switch (kind) {
        case collection::Playlist:
            return facade.recentPlaylists(limit);
        case collection::Album:
            return facade.recentAlbums(limit);
        case collection::Podcast:
            return facade.recentPodcasts(limit);
        default:
            return MusicRequests::Refused;
        }
    };
    switch (kind) {
    case collection::Playlist:
        return read<domain::CloudRecentPlaylists>(
            m_requests, send, std::move(reply), [](const domain::CloudRecentPlaylists& found) {
                return entitiesOf<CollectionItem>(found, playlistOf);
            });
    case collection::Album:
        return read<domain::CloudRecentAlbums>(
            m_requests, send, std::move(reply), [](const domain::CloudRecentAlbums& found) {
                return entitiesOf<CollectionItem>(found, albumOf);
            });
    case collection::Podcast:
        return read<domain::CloudRecentPodcasts>(
            m_requests, send, std::move(reply), [](const domain::CloudRecentPodcasts& found) {
                return entitiesOf<CollectionItem>(found, podcastOf);
            });
    default:
        // The service keeps no record of artists played.
        return replyLater(this, std::move(reply),
                          Outcome<QList<CollectionItem>>{QList<CollectionItem>{}});
    }
}

Request BackendLibrary::recentVoices(int limit, Reply<QList<SongInfo>> reply)
{
    return read<domain::CloudRecentVoices>(
        m_requests,
        [limit = std::clamp(limit, 1, 100)](BackendFacade& facade) {
            return signedIn(facade) ? facade.recentVoices(limit) : MusicRequests::NeedsSignIn;
        },
        std::move(reply),
        [](const domain::CloudRecentVoices& found) {
            return entitiesOf<SongInfo>(found, voiceOf);
        });
}

Request BackendLibrary::listeningRanking(bool allTime, Reply<QList<SongInfo>> reply)
{
    const auto period = allTime ? domain::RankingPeriod::AllTime : domain::RankingPeriod::LastWeek;
    return readWithCovers<domain::ListeningRanking>(
        m_requests,
        [period](BackendFacade& facade) {
            return signedIn(facade) ? facade.listeningRanking(period) : MusicRequests::NeedsSignIn;
        },
        std::move(reply),
        [](const domain::ListeningRanking& found) {
            QList<SongInfo> songs;
            songs.reserve(found.songs.size());
            for (const auto& ranked : found.songs)
                songs.append(songOf(ranked.song));
            return songs;
        },
        listSongs);
}

Request BackendLibrary::write(QList<CollectionWrites::Step> steps, Reply<bool> reply, bool value)
{
    return m_writes->submit(
        std::move(steps),
        [reply, value](const domain::CollectionResult&) { reply({value, false, std::nullopt}); },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply({std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

Request BackendLibrary::createPlaylist(const QString& title, Reply<QString> reply)
{
    const domain::PlaylistCreate change{title};
    return m_writes->submit(
        {[change](BackendFacade&) { return domain::CollectionChange(change); }},
        [reply](const domain::CollectionResult& result) {
            if (result.createdPlaylist)
                reply({result.createdPlaylist->value, false, std::nullopt});
            else
                reply({std::nullopt, false, Failure{failure::Unknown, std::nullopt}});
        },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply({std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

Request BackendLibrary::setSongLiked(const QString& key, bool liked, Reply<bool> reply)
{
    const domain::SongHeart change{domain::SongId{key}, liked};
    return write({[change](BackendFacade&) { return domain::CollectionChange(change); }},
                 std::move(reply));
}

namespace
{
// One step per 100 songs, each song once.
QList<CollectionWrites::Step> songSteps(const QString& playlistKey, const QStringList& songKeys,
                                        domain::CollectionAction action)
{
    QList<domain::SongId> ids;
    QSet<QString> seen;
    for (const QString& key : songKeys)
        if (!seen.contains(key)) {
            seen.insert(key);
            ids.append(domain::SongId{key});
        }
    QList<CollectionWrites::Step> steps;
    for (qsizetype at = 0; at < ids.size(); at += kPage) {
        const domain::PlaylistCollection change{domain::PlaylistId{playlistKey}, ids.mid(at, kPage),
                                                action};
        steps.append([change](BackendFacade&) { return domain::CollectionChange(change); });
    }
    return steps;
}
} // namespace

Request BackendLibrary::addToPlaylist(const QString& playlistKey, const QStringList& songKeys,
                                      Reply<bool> reply)
{
    return write(songSteps(playlistKey, songKeys, domain::CollectionAction::Collect),
                 std::move(reply));
}

Request BackendLibrary::removeFromPlaylist(const QString& playlistKey, const QStringList& songKeys,
                                           Reply<bool> reply)
{
    return write(songSteps(playlistKey, songKeys, domain::CollectionAction::Remove),
                 std::move(reply));
}

Request BackendLibrary::reorderPlaylist(const QString& playlistKey, const QStringList& songKeys,
                                        Reply<bool> reply)
{
    QList<domain::SongId> ordered;
    ordered.reserve(songKeys.size());
    for (const QString& key : songKeys)
        ordered.append(domain::SongId{key});
    auto current = std::make_shared<Request>();
    *current = m_requests->submit(
        [playlist = domain::PlaylistId{playlistKey}](BackendFacade& facade) {
            return signedIn(facade) ? facade.playlistDetails(playlist) : MusicRequests::NeedsSignIn;
        },
        [weak = std::weak_ptr<Request>(current), writes = QPointer<CollectionWrites>(m_writes),
         ordered, reply](const application::MusicResult& result) {
            const auto current = weak.lock();
            const auto* details = std::get_if<domain::PlaylistDetails>(&result);
            if (!current || !writes || !details) {
                reply({std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
                return;
            }
            // The order is of the songs read now, in this session.
            auto step = [details = *details, ordered](
                            BackendFacade& facade) -> std::optional<domain::CollectionChange> {
                if (auto order = domain::PlaylistSongOrder::fromDetails(facade.snapshot().token,
                                                                        details, ordered))
                    return domain::CollectionChange(std::move(*order));
                return std::nullopt;
            };
            *current = writes->submit(
                {std::move(step)},
                [reply](const domain::CollectionResult&) { reply({true, false, std::nullopt}); },
                [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
                    reply({std::nullopt, false, Failure{kind, retryAfterMs}});
                });
        },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply({std::nullopt, false, Failure{kind, retryAfterMs}});
        });
    return Request([current] { current->cancel(); });
}

Request BackendLibrary::updatePlaylist(const QString& playlistKey, const QString& title,
                                       const QString& description, Reply<bool> reply)
{
    const domain::PlaylistId playlist{playlistKey};
    const domain::PlaylistRename rename{playlist, title};
    const domain::PlaylistDescription text{playlist, description};
    return write({[rename](BackendFacade&) { return domain::CollectionChange(rename); },
                  [text](BackendFacade&) { return domain::CollectionChange(text); }},
                 std::move(reply));
}

Request BackendLibrary::deletePlaylist(const QString& playlistKey, Reply<bool> reply)
{
    const domain::PlaylistDelete change{domain::PlaylistId{playlistKey}};
    return write({[change](BackendFacade&) { return domain::CollectionChange(change); }},
                 std::move(reply));
}

} // namespace cirrus::ui
