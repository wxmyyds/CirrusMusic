#pragma once

#include "music_requests.h"
#include "types.h"

#include "application/backend/backend_facade.h"
#include "domain/music/entity_search_types.h"
#include "domain/music/playlist_discovery_types.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QTimeZone>
#include <QTimer>

#include <memory>

// What the backend adapters share (docs/ui/integration.md §2): reads through
// MusicRequests mapped to the presentation's types, and the mappings themselves.
namespace cirrus::ui::mapping
{

using application::BackendFacade;
using application::MusicResult;

// Replies after the call returned (never synchronously), unless canceled first.
template <class T> Request replyLater(QObject* context, Reply<T> reply, Outcome<T> outcome)
{
    auto alive = std::make_shared<bool>(true);
    QTimer::singleShot(0, context, [alive, reply = std::move(reply), outcome = std::move(outcome)] {
        if (*alive)
            reply(outcome);
    });
    return Request([alive] { *alive = false; });
}

// One read whose result is an R, mapped to the T the page wants.
template <class R, class T, class Map>
Request read(MusicRequests* requests, MusicRequests::Send send, Reply<T> reply, Map map)
{
    return requests->submit(
        std::move(send),
        [reply, map](const MusicResult& result) {
            if (const auto* value = std::get_if<R>(&result))
                reply(Outcome<T>{map(*value), false, std::nullopt});
            else
                reply(Outcome<T>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
        },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply(Outcome<T>{std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

inline QString coverKey(const QUrl& url) { return url.isEmpty() ? QString() : url.toString(); }

inline int yearOf(const std::optional<qint64>& ms)
{
    return ms ? QDateTime::fromMSecsSinceEpoch(*ms, QTimeZone::UTC).date().year() : 0;
}

inline QList<ArtistRef> artistsOf(const QList<domain::ArtistCredit>& credits)
{
    QList<ArtistRef> artists;
    artists.reserve(credits.size());
    for (const auto& credit : credits)
        artists.append({credit.id ? credit.id->value : QString(), credit.name});
    return artists;
}

inline QString bylineOf(const QList<domain::ArtistCredit>& credits)
{
    QStringList names;
    for (const auto& credit : credits)
        names.append(credit.name);
    return names.join(QStringLiteral(" / "));
}

inline SongInfo songOf(const domain::Song& song)
{
    SongInfo info;
    info.key = song.id.value;
    info.title = song.title;
    if (song.translatedTitles && !song.translatedTitles->isEmpty())
        info.secondaryTitle = song.translatedTitles->first();
    else if (song.aliases && !song.aliases->isEmpty())
        info.secondaryTitle = song.aliases->first();
    info.artists = artistsOf(song.artists);
    if (song.album) {
        info.albumKey = song.album->id ? song.album->id->value : QString();
        info.albumTitle = song.album->title;
        info.coverKey = coverKey(song.album->cover);
    }
    info.durationMs = song.durationMs.value_or(-1);
    info.publishedAtMs = song.publishedAtMs.value_or(0);
    return info;
}

inline CollectionItem albumOf(const domain::Album& album)
{
    CollectionItem item;
    item.key = album.id.value;
    item.kind = collection::Album;
    item.title = album.title;
    item.byline = bylineOf(album.artists);
    item.year = yearOf(album.publishedAtMs);
    item.count = album.trackCount ? int(*album.trackCount) : -1;
    item.coverKey = coverKey(album.cover);
    return item;
}

inline CollectionItem artistOf(const domain::SearchArtist& artist)
{
    CollectionItem item;
    item.key = artist.id.value;
    item.kind = collection::Artist;
    item.title = artist.name;
    item.count = artist.albumCount ? int(*artist.albumCount) : -1;
    item.coverKey = coverKey(artist.portrait);
    return item;
}

inline CollectionItem playlistOf(const domain::DiscoveryPlaylist& playlist)
{
    CollectionItem item;
    item.key = playlist.id.value;
    item.kind = collection::Playlist;
    item.title = playlist.title;
    item.count = playlist.trackCount ? int(*playlist.trackCount) : -1;
    item.coverKey = coverKey(playlist.cover);
    item.playCount = playlist.playCount ? qint64(*playlist.playCount) : -1;
    return item;
}

inline CollectionItem podcastOf(const domain::Podcast& podcast)
{
    CollectionItem item;
    item.key = podcast.id.value;
    item.kind = collection::Podcast;
    item.title = podcast.title;
    item.count = podcast.voiceCount ? int(*podcast.voiceCount) : -1;
    item.coverKey = coverKey(podcast.cover);
    return item;
}

// Podcast voices the backend sources have read, by key, with their tracks:
// playVoices() is given keys only. Caller (UI) thread; bounded.
class KnownVoices
{
  public:
    static void remember(const SongInfo& voice)
    {
        auto& known = voices();
        if (!voice.voice || voice.key.isEmpty())
            return;
        if (known.size() >= 4096 && !known.contains(voice.key))
            known.clear();
        known.insert(voice.key, voice);
    }
    [[nodiscard]] static std::optional<SongInfo> find(const QString& key)
    {
        const auto& known = voices();
        const auto it = known.constFind(key);
        return it == known.cend() ? std::nullopt : std::optional(*it);
    }

  private:
    static QHash<QString, SongInfo>& voices()
    {
        static QHash<QString, SongInfo> known;
        return known;
    }
};

inline SongInfo voiceOf(const domain::Voice& voice)
{
    SongInfo info;
    info.key = voice.id.value;
    info.title = voice.title;
    info.voice = true;
    info.coverKey = coverKey(voice.cover);
    if (voice.podcast) {
        info.albumKey = voice.podcast->id.value;
        info.albumTitle = voice.podcast->title;
        if (info.coverKey.isEmpty())
            info.coverKey = coverKey(voice.podcast->cover);
    }
    // The provider's duration is raw metadata; milliseconds in what was seen so far.
    info.durationMs = voice.providerDuration.value_or(-1);
    info.trackKey = voice.track ? voice.track->value : QString();
    KnownVoices::remember(info);
    return info;
}

template <class Page, class Item, class Map>
ResultPage<Item> pageOf(const Page& page, const auto& entries, Map map)
{
    ResultPage<Item> result;
    result.items.reserve(entries.size());
    for (const auto& entry : entries)
        result.items.append(map(entry));
    result.hasMore = page.hasMore;
    result.total = page.total;
    return result;
}

// Fills in the covers `songs` lack from one song-details read, then replies; replies
// with `value` as it is when that read fails. Some reads (old search, artist songs)
// give no album covers.
template <class T>
void replyWithCovers(MusicRequests* requests, std::shared_ptr<Request> current, T value,
                     std::function<QList<SongInfo>*(T&)> songsOf, Reply<T> reply)
{
    QList<domain::SongId> ids;
    QStringList seen;
    for (const SongInfo& song : std::as_const(*songsOf(value))) {
        if (song.coverKey.isEmpty() && domain::validMusicId(song.key) && !seen.contains(song.key) &&
            ids.size() < kSongDetailsBatch) {
            seen.append(song.key);
            ids.append(domain::SongId{song.key});
        }
    }
    if (ids.isEmpty() || !requests) {
        reply(Outcome<T>{std::move(value), false, std::nullopt});
        return;
    }
    auto shared = std::make_shared<T>(std::move(value));
    *current =
        requests->submit([ids](BackendFacade& facade) { return facade.songDetails(ids); },
                         [shared, songsOf, reply](const MusicResult& result) {
                             if (const auto* details = std::get_if<domain::SongDetails>(&result)) {
                                 QHash<QString, QString> covers;
                                 for (const auto& song : details->songs)
                                     if (song.album && !song.album->cover.isEmpty())
                                         covers.insert(song.id.value, coverKey(song.album->cover));
                                 for (SongInfo& song : *songsOf(*shared))
                                     if (song.coverKey.isEmpty())
                                         song.coverKey = covers.value(song.key);
                             }
                             reply(Outcome<T>{*shared, false, std::nullopt});
                         },
                         [shared, reply](failure::Kind, std::optional<qint64>) {
                             reply(Outcome<T>{*shared, false, std::nullopt});
                         });
}

// A read of an R mapped to T whose songs then get their missing covers.
template <class R, class T, class Map>
Request readWithCovers(MusicRequests* requests, MusicRequests::Send send, Reply<T> reply, Map map,
                       std::type_identity_t<std::function<QList<SongInfo>*(T&)>> songsOf)
{
    auto current = std::make_shared<Request>();
    auto guard = QPointer<MusicRequests>(requests);
    *current = read<R>(
        requests, std::move(send),
        Reply<T>([weak = std::weak_ptr<Request>(current), guard, songsOf, reply](Outcome<T> found) {
            const auto current = weak.lock();
            if (!current || !found.value) {
                reply(std::move(found));
                return;
            }
            replyWithCovers<T>(guard.data(), current, std::move(*found.value), songsOf, reply);
        }),
        map);
    return Request([current] { *current = Request(); });
}

inline QList<SongInfo>* pageSongs(ResultPage<SongInfo>& page) { return &page.items; }
inline QList<SongInfo>* listSongs(QList<SongInfo>& songs) { return &songs; }

// The facade won't take a signed-in-only read from a guest; say so rather than wait.
inline bool signedIn(const BackendFacade& facade)
{
    return facade.snapshot().phase == domain::SessionPhase::Authenticated;
}

} // namespace cirrus::ui::mapping
