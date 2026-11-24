#include "backend_catalog.h"

#include "backend_mapping.h"
#include "music_requests.h"

#include "application/backend/backend_facade.h"
#include "domain/music/discovery_browse_types.h"
#include "domain/music/discovery_types.h"
#include "domain/music/introduction_types.h"
#include "domain/music/recommendation_history_types.h"
#include "domain/music/song_wiki_text_types.h"
#include "infrastructure/runtime/backend_runtime.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QTimeZone>
#include <QTimer>

namespace cirrus::ui
{
namespace
{

using namespace mapping;

// The backend's search page, or nullopt past what the service pages through.
std::optional<domain::SearchPage> searchPage(const QString& text, PageRequest page)
{
    domain::SearchPage query{text.trimmed(), std::clamp(page.limit, 1, 100), page.offset};
    if (!query.isValid())
        return std::nullopt;
    return query;
}

CollectionItem searchArtistOf(const domain::SearchArtist& artist) { return artistOf(artist); }

// The two wiki reads overlap and the service gives one row per person: rows next to each
// other with the same label become one ("作曲: A / B"), and a row saying what a tag card
// already says (曲风, BPM) is left out. Sections left empty go.
void tidyWiki(SongWiki& wiki)
{
    QHash<QString, QString> cardText;
    for (const SongWiki::Card& card : std::as_const(wiki.cards))
        cardText.insert(card.title.trimmed(), card.values.join(QStringLiteral(" / ")));
    QList<SongWiki::Section> sections;
    for (SongWiki::Section& section : wiki.sections) {
        QList<SongWiki::Row> rows;
        for (const SongWiki::Row& row : std::as_const(section.rows)) {
            const QString label = row.label.trimmed();
            if (cardText.value(label) == row.text && cardText.contains(label))
                continue;
            if (!rows.isEmpty() && rows.last().label.trimmed() == label)
                rows.last().text += QStringLiteral(" / ") + row.text;
            else
                rows.append(row);
        }
        section.rows = rows;
        if (!section.rows.isEmpty() || !section.description.trimmed().isEmpty())
            sections.append(std::move(section));
    }
    wiki.sections = std::move(sections);
}

SongInfo programOf(const domain::Program& program)
{
    SongInfo info;
    info.key = program.id.value;
    info.title = program.title;
    info.voice = true;
    info.coverKey = coverKey(program.cover);
    if (program.podcast) {
        info.albumKey = program.podcast->id.value;
        info.albumTitle = program.podcast->title;
        if (info.coverKey.isEmpty())
            info.coverKey = coverKey(program.podcast->cover);
    }
    info.durationMs = program.providerDuration.value_or(-1);
    info.serial = program.serialNumber ? int(*program.serialNumber) : -1;
    info.trackKey = program.track ? program.track->value : QString();
    KnownVoices::remember(info);
    return info;
}

ArtistInfo artistInfoOf(const domain::ArtistDetails& artist)
{
    ArtistInfo info;
    info.key = artist.id.value;
    info.name = artist.name;
    info.description = artist.description;
    info.avatarKey = coverKey(artist.portrait);
    info.songCount = artist.songCount ? int(*artist.songCount) : -1;
    info.albumCount = artist.albumCount ? int(*artist.albumCount) : -1;
    info.aliases =
        artist.translatedNames.value_or(QStringList()) + artist.aliases.value_or(QStringList());
    info.userKey = artist.userId.value_or(QString());
    return info;
}

AlbumPage albumPageOf(const domain::AlbumDetails& details)
{
    AlbumPage page;
    const auto& album = details.album;
    page.info.key = album.id.value;
    page.info.title = album.title;
    page.info.artists = artistsOf(album.artists);
    page.info.year = yearOf(album.publishedAtMs);
    page.info.coverKey = coverKey(album.cover);
    page.info.description = album.description;
    page.info.type = album.subType.value_or(album.type.value_or(QString()));
    page.info.aliases = album.aliases.value_or(QStringList());
    page.info.publishedAtMs = album.publishedAtMs.value_or(0);
    page.info.company = album.company.value_or(QString());
    for (const auto& song : details.songs) {
        SongInfo info = songOf(song);
        if (info.coverKey.isEmpty())
            info.coverKey = page.info.coverKey; // an album's songs share its cover
        page.songKeys.append(info.key);
        page.songs.append(info);
    }
    return page;
}

} // namespace

BackendCatalog::BackendCatalog(runtime::BackendRuntime& runtime, QObject* parent)
    : QObject(parent), m_requests(new MusicRequests(runtime.facade(), this)),
      m_historyDates(std::make_shared<std::shared_ptr<const domain::DailyHistoryDates>>())
{
    m_session = [facade = QPointer<BackendFacade>(&runtime.facade())]() {
        if (!facade || facade->snapshot().phase != domain::SessionPhase::Authenticated)
            return QString();
        return facade->snapshot().accountId;
    };
}

BackendCatalog::~BackendCatalog() = default;

Request BackendCatalog::suggestions(const QString& text, Reply<QList<SearchSuggestion>> reply)
{
    const domain::SearchSuggestionsQuery query{text.trimmed()};
    if (!query.isValid())
        return replyLater(this, std::move(reply),
                          Outcome<QList<SearchSuggestion>>{QList<SearchSuggestion>{}});
    return read<domain::SearchSuggestions>(
        m_requests,
        [keywords = query.keywords](BackendFacade& facade) {
            return facade.searchSuggestions(keywords);
        },
        std::move(reply),
        [](const domain::SearchSuggestions& found) {
            QList<SearchSuggestion> list;
            QList<domain::SuggestionGroup> done;
            // In the service's group order, each group once.
            for (const auto group : found.order) {
                if (done.contains(group))
                    continue;
                done.append(group);
                switch (group) {
                case domain::SuggestionGroup::Song:
                    for (const auto& song :
                         found.songs.value_or(QList<domain::SearchSuggestion<domain::SongId>>{}))
                        list.append({true, collection::Playlist, song.id.value, song.title});
                    break;
                case domain::SuggestionGroup::Artist:
                    for (const auto& artist : found.artists.value_or(
                             QList<domain::SearchSuggestion<domain::ArtistId>>{}))
                        list.append({false, collection::Artist, artist.id.value, artist.title});
                    break;
                case domain::SuggestionGroup::Album:
                    for (const auto& album :
                         found.albums.value_or(QList<domain::SearchSuggestion<domain::AlbumId>>{}))
                        list.append({false, collection::Album, album.id.value, album.title});
                    break;
                case domain::SuggestionGroup::Playlist:
                    for (const auto& playlist : found.playlists.value_or(
                             QList<domain::SearchSuggestion<domain::PlaylistId>>{}))
                        list.append(
                            {false, collection::Playlist, playlist.id.value, playlist.title});
                    break;
                }
            }
            return list;
        });
}

Request BackendCatalog::hotKeywords(Reply<QList<HotKeyword>> reply)
{
    // The detailed list: it has the service's note for each keyword.
    return read<domain::HotKeywordDetails>(
        m_requests, [](BackendFacade& facade) { return facade.hotKeywordDetails(); },
        std::move(reply),
        [](const domain::HotKeywordDetails& details) {
            QList<HotKeyword> list;
            list.reserve(details.entries.size());
            for (const auto& entry : details.entries)
                list.append({entry.keyword, entry.description.value_or(QString())});
            return list;
        });
}

Request BackendCatalog::searchSongs(const QString& text, PageRequest page,
                                    Reply<ResultPage<SongInfo>> reply)
{
    const auto query = searchPage(text, page);
    if (!query)
        return replyLater(this, std::move(reply),
                          Outcome<ResultPage<SongInfo>>{ResultPage<SongInfo>{{}, false, {}, {}}});
    return readWithCovers<domain::SongSearchPage>(
        m_requests,
        [query = *query](BackendFacade& facade) {
            return facade.searchSongs({query.keywords, query.limit, query.offset});
        },
        std::move(reply),
        [](const domain::SongSearchPage& found) {
            return pageOf<domain::SongSearchPage, SongInfo>(found, found.songs, songOf);
        },
        pageSongs);
}

Request BackendCatalog::searchCollections(collection::Kind kind, const QString& text,
                                          PageRequest page, Reply<ResultPage<CollectionItem>> reply)
{
    const auto query = searchPage(text, page);
    if (!query)
        return replyLater(
            this, std::move(reply),
            Outcome<ResultPage<CollectionItem>>{ResultPage<CollectionItem>{{}, false, {}, {}}});
    switch (kind) {
    case collection::Album:
        return read<domain::AlbumSearchPage>(
            m_requests, [query = *query](BackendFacade& f) { return f.searchAlbums(query); },
            std::move(reply),
            [](const domain::AlbumSearchPage& found) {
                return pageOf<domain::AlbumSearchPage, CollectionItem>(found, found.albums,
                                                                       albumOf);
            });
    case collection::Artist:
        return read<domain::ArtistSearchPage>(
            m_requests, [query = *query](BackendFacade& f) { return f.searchArtists(query); },
            std::move(reply),
            [](const domain::ArtistSearchPage& found) {
                return pageOf<domain::ArtistSearchPage, CollectionItem>(found, found.artists,
                                                                        artistOf);
            });
    case collection::Playlist:
        return read<domain::PlaylistSearchPage>(
            m_requests, [query = *query](BackendFacade& f) { return f.searchPlaylists(query); },
            std::move(reply),
            [](const domain::PlaylistSearchPage& found) {
                return pageOf<domain::PlaylistSearchPage, CollectionItem>(found, found.playlists,
                                                                          playlistOf);
            });
    case collection::Podcast:
        return read<domain::PodcastSearchPage>(
            m_requests, [query = *query](BackendFacade& f) { return f.searchPodcasts(query); },
            std::move(reply),
            [](const domain::PodcastSearchPage& found) {
                return pageOf<domain::PodcastSearchPage, CollectionItem>(found, found.podcasts,
                                                                         podcastOf);
            });
    }
    return replyLater(this, std::move(reply),
                      Outcome<ResultPage<CollectionItem>>{std::nullopt, false,
                                                          Failure{failure::Invalid, std::nullopt}});
}

Request BackendCatalog::searchVoices(const QString& text, PageRequest page,
                                     Reply<ResultPage<SongInfo>> reply)
{
    const auto query = searchPage(text, page);
    if (!query)
        return replyLater(this, std::move(reply),
                          Outcome<ResultPage<SongInfo>>{ResultPage<SongInfo>{{}, false, {}, {}}});
    return read<domain::VoiceSearchPage>(
        m_requests, [query = *query](BackendFacade& f) { return f.searchVoices(query); },
        std::move(reply),
        [](const domain::VoiceSearchPage& found) {
            return pageOf<domain::VoiceSearchPage, SongInfo>(found, found.voices, voiceOf);
        });
}

// Step 5: details.

Request BackendCatalog::playlist(const QString& key, Reply<PlaylistPage> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(
            this, std::move(reply),
            Outcome<PlaylistPage>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
    return m_requests->submit(
        [key](BackendFacade& facade) { return facade.playlistDetails({key}); },
        [this, reply, guard = QPointer<BackendCatalog>(this)](const MusicResult& result) {
            const auto* details = std::get_if<domain::PlaylistDetails>(&result);
            if (!details || !guard) {
                reply(Outcome<PlaylistPage>{std::nullopt, false,
                                            Failure{failure::Rejected, std::nullopt}});
                return;
            }
            PlaylistPage page;
            auto& info = page.info;
            info.key = details->id.value;
            info.title = details->title;
            info.description = details->description;
            info.creator = details->creator ? details->creator->nickname : QString();
            info.creatorKey = details->creator && details->creator->publicUserId
                                  ? *details->creator->publicUserId
                                  : QString();
            info.coverKey = coverKey(details->cover);
            info.tags = details->tags.value_or(QStringList());
            info.updatedAtMs = details->updatedAtMs.value_or(0);
            info.playCount = details->playCount ? qint64(*details->playCount) : -1;
            info.subscriberCount = details->subscriberCount.value_or(-1);
            info.commentCount = details->commentCount.value_or(-1);
            const QString account = m_session ? m_session() : QString();
            info.own =
                !account.isEmpty() && details->creator && details->creator->publicUserId == account;
            for (const auto& song : details->songs)
                page.songs.append(songOf(song));
            // The whole index when the service proves it; else what came embedded.
            bool partial = false;
            if (details->trackIds && details->trackIdsComplete) {
                for (const auto& id : *details->trackIds)
                    page.songKeys.append(id.value);
            } else {
                for (const SongInfo& song : std::as_const(page.songs))
                    page.songKeys.append(song.key);
                partial = details->trackCount && *details->trackCount > page.songKeys.size();
            }
            reply(Outcome<PlaylistPage>{std::move(page), partial, std::nullopt});
        },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply(Outcome<PlaylistPage>{std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

Request BackendCatalog::songDetails(const QStringList& keys, Reply<QList<SongInfo>> reply)
{
    QList<domain::SongId> ids;
    QStringList seen;
    for (const QString& key : keys.mid(0, kSongDetailsBatch)) {
        if (domain::validMusicId(key) && !seen.contains(key)) {
            seen.append(key);
            ids.append(domain::SongId{key});
        }
    }
    if (ids.isEmpty())
        return replyLater(this, std::move(reply), Outcome<QList<SongInfo>>{QList<SongInfo>{}});
    return read<domain::SongDetails>(
        m_requests, [ids](BackendFacade& facade) { return facade.songDetails(ids); },
        std::move(reply),
        [](const domain::SongDetails& details) {
            QList<SongInfo> songs;
            songs.reserve(details.songs.size());
            for (const auto& song : details.songs)
                songs.append(songOf(song)); // missing ones are left out (the interface)
            return songs;
        });
}

Request BackendCatalog::album(const QString& key, Reply<AlbumPage> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(
            this, std::move(reply),
            Outcome<AlbumPage>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
    return m_requests->submit(
        [key](BackendFacade& facade) { return facade.albumDetails({key}); },
        [reply](const MusicResult& result) {
            if (const auto* details = std::get_if<domain::AlbumDetails>(&result))
                reply(Outcome<AlbumPage>{albumPageOf(*details), !details->complete, std::nullopt});
            else
                reply(Outcome<AlbumPage>{std::nullopt, false,
                                         Failure{failure::Rejected, std::nullopt}});
        },
        [reply](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            reply(Outcome<AlbumPage>{std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

Request BackendCatalog::artist(const QString& key, Reply<ArtistPage> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(
            this, std::move(reply),
            Outcome<ArtistPage>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
    // The header and the popular songs, at once; the page fails only with the header.
    struct Join {
        std::optional<Outcome<ArtistInfo>> info;
        std::optional<QList<SongInfo>> top;
        Request infoRead, topRead;
        Reply<ArtistPage> reply;
        void finish()
        {
            if (!info || !top || !reply)
                return;
            auto done = std::exchange(reply, {});
            if (!info->value) {
                done(Outcome<ArtistPage>{std::nullopt, false, info->failure});
                return;
            }
            done(Outcome<ArtistPage>{ArtistPage{*info->value, *top}, false, std::nullopt});
        }
    };
    auto join = std::make_shared<Join>();
    join->reply = std::move(reply);
    std::weak_ptr<Join> weak = join;
    join->infoRead = read<domain::ArtistDetails>(
        m_requests, [key](BackendFacade& facade) { return facade.artistDetails({key}); },
        Reply<ArtistInfo>([weak](Outcome<ArtistInfo> info) {
            if (const auto join = weak.lock()) {
                join->info = std::move(info);
                join->finish();
            }
        }),
        artistInfoOf);
    join->topRead = readWithCovers<domain::DiscoveryBrowsePage>(
        m_requests,
        [key](BackendFacade& facade) {
            return facade.discover({domain::ArtistTopSongsQuery{{key}}});
        },
        Reply<ResultPage<SongInfo>>([weak](Outcome<ResultPage<SongInfo>> top) {
            if (const auto join = weak.lock()) {
                join->top = top.value ? top.value->items : QList<SongInfo>();
                join->finish();
            }
        }),
        [](const domain::DiscoveryBrowsePage& page) {
            ResultPage<SongInfo> top;
            if (const auto* songs = std::get_if<domain::DiscoverySongs>(&page.content))
                for (const auto& song : songs->songs.mid(0, 50))
                    top.items.append(songOf(song));
            return top;
        },
        pageSongs);
    return Request([join] {
        join->reply = {};
        join->infoRead = Request();
        join->topRead = Request();
    });
}

Request BackendCatalog::artistSongs(const QString& key, bool byTime, PageRequest page,
                                    Reply<ResultPage<SongInfo>> reply)
{
    const domain::ArtistSongsQuery query{{key},
                                         {std::clamp(page.limit, 1, 100), page.offset},
                                         byTime ? domain::ArtistSongOrder::Time
                                                : domain::ArtistSongOrder::Hot};
    if (!query.isValid())
        return replyLater(this, std::move(reply),
                          Outcome<ResultPage<SongInfo>>{ResultPage<SongInfo>{{}, false, {}, {}}});
    return readWithCovers<domain::ArtistSongsPage>(
        m_requests, [query](BackendFacade& facade) { return facade.artistSongs(query); },
        std::move(reply),
        [](const domain::ArtistSongsPage& found) {
            return pageOf<domain::ArtistSongsPage, SongInfo>(found, found.songs, songOf);
        },
        pageSongs);
}

Request BackendCatalog::artistAlbums(const QString& key, PageRequest page,
                                     Reply<ResultPage<CollectionItem>> reply)
{
    const domain::ArtistAlbumsQuery query{{key}, {std::clamp(page.limit, 1, 100), page.offset}};
    if (!query.isValid())
        return replyLater(
            this, std::move(reply),
            Outcome<ResultPage<CollectionItem>>{ResultPage<CollectionItem>{{}, false, {}, {}}});
    return read<domain::ArtistAlbumsPage>(
        m_requests, [query](BackendFacade& facade) { return facade.artistAlbums(query); },
        std::move(reply),
        [](const domain::ArtistAlbumsPage& found) {
            return pageOf<domain::ArtistAlbumsPage, CollectionItem>(found, found.albums, albumOf);
        });
}

Request BackendCatalog::artistIntroduction(const QString& key, Reply<ArtistIntroduction> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(this, std::move(reply),
                          Outcome<ArtistIntroduction>{std::nullopt, false,
                                                      Failure{failure::Rejected, std::nullopt}});
    return read<domain::ArtistIntroduction>(
        m_requests, [key](BackendFacade& facade) { return facade.artistIntroduction({key}); },
        std::move(reply),
        [](const domain::ArtistIntroduction& found) {
            ArtistIntroduction intro;
            intro.brief = found.briefDescription.value_or(QString());
            for (const auto& section : found.sections)
                intro.sections.append({section.title.value_or(QString()), section.text});
            return intro;
        });
}

Request BackendCatalog::similarArtists(const QString& key, Reply<QList<CollectionItem>> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(this, std::move(reply),
                          Outcome<QList<CollectionItem>>{QList<CollectionItem>{}});
    return read<domain::DiscoveryBrowsePage>(
        m_requests,
        [key](BackendFacade& facade) {
            return facade.discover({domain::SimilarArtistsQuery{{key}}});
        },
        std::move(reply),
        [](const domain::DiscoveryBrowsePage& page) {
            QList<CollectionItem> artists;
            if (const auto* found = std::get_if<domain::DiscoveryArtists>(&page.content))
                for (const auto& artist : found->artists)
                    artists.append(searchArtistOf(artist));
            return artists;
        });
}

// Both of the service's wiki reads (docs/ui/pages/song.md §1): the tag cards, and the
// two text sections. Each may fail or be empty on its own; the wiki fails only when
// both reads do.
Request BackendCatalog::songWiki(const SongInfo& song, Reply<SongWiki> reply)
{
    if (!domain::validMusicId(song.key) || !domain::validMusicId(song.albumKey) ||
        song.title.isEmpty())
        return replyLater(
            this, std::move(reply),
            Outcome<SongWiki>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});

    struct State {
        SongWiki wiki;
        int pending = 2;
        int failed = 0;
        std::optional<Failure> failure;
        Reply<SongWiki> reply;
        Request cards;
        Request sections;
    };
    auto state = std::make_shared<State>();
    state->reply = std::move(reply);
    const auto settle = [](const std::shared_ptr<State>& state, std::optional<Failure> failure) {
        if (failure) {
            ++state->failed;
            state->failure = failure;
        }
        if (--state->pending > 0)
            return;
        state->cards.release();
        state->sections.release();
        if (state->failed == 2) {
            state->reply(Outcome<SongWiki>{std::nullopt, false, state->failure});
            return;
        }
        tidyWiki(state->wiki);
        state->reply(Outcome<SongWiki>{std::move(state->wiki)});
    };

    const QString key = song.key;
    state->cards = read<domain::SongWikiSummary>(
        m_requests, [key](BackendFacade& facade) { return facade.songWikiSummary({key}); },
        Reply<QList<SongWiki::Card>>([state, settle](Outcome<QList<SongWiki::Card>> outcome) {
            if (outcome.value)
                state->wiki.cards = std::move(*outcome.value);
            settle(state, outcome.value ? std::nullopt : outcome.failure);
        }),
        [](const domain::SongWikiSummary& summary) {
            QList<SongWiki::Card> cards;
            for (const auto& card : summary.cards) {
                QStringList values;
                for (const QString& value : card.values)
                    if (!value.trimmed().isEmpty())
                        values << value;
                if (values.isEmpty())
                    continue;
                QString title = card.title;
                if (title.trimmed().isEmpty()) {
                    switch (card.kind) {
                    case domain::SongWikiKind::Tags:
                        title = BackendCatalog::tr("曲风");
                        break;
                    case domain::SongWikiKind::BusinessTags:
                        title = BackendCatalog::tr("标签");
                        break;
                    case domain::SongWikiKind::Language:
                        title = BackendCatalog::tr("语种");
                        break;
                    case domain::SongWikiKind::Bpm:
                        title = QStringLiteral("BPM");
                        break;
                    }
                }
                cards.append({title, values});
            }
            return cards;
        });

    // The text read wants the song as the service gave it and the session it was read on.
    domain::Song source;
    source.id = {song.key};
    source.title = song.title;
    source.album = domain::AlbumReference{domain::AlbumId{song.albumKey}, song.albumTitle, {}};
    state->sections = read<domain::SongWikiText>(
        m_requests,
        [source](BackendFacade& facade) -> quint64 {
            const auto query = domain::SongWikiTextQuery::fromSong(facade.snapshot().token, source);
            return query ? facade.songWikiText(*query) : MusicRequests::Refused;
        },
        Reply<QList<SongWiki::Section>>([state, settle](Outcome<QList<SongWiki::Section>> outcome) {
            if (outcome.value)
                state->wiki.sections = std::move(*outcome.value);
            settle(state, outcome.value ? std::nullopt : outcome.failure);
        }),
        [](const domain::SongWikiText& text) {
            QList<SongWiki::Section> sections;
            for (const auto& found : text.sections) {
                SongWiki::Section section{found.label, found.description.value_or(QString()), {}};
                for (const auto& item : found.items) {
                    QStringList parts;
                    if (item.content && !item.content->trimmed().isEmpty())
                        parts << *item.content;
                    for (const QString& part : item.texts.value_or(QStringList()))
                        if (!part.trimmed().isEmpty())
                            parts << part;
                    if (!parts.isEmpty())
                        section.rows.append({item.label, parts.join(QStringLiteral(" / "))});
                }
                if (!section.rows.isEmpty() || !section.description.trimmed().isEmpty())
                    sections.append(std::move(section));
            }
            return sections;
        });

    return Request([state] {
        state->cards.cancel();
        state->sections.cancel();
    });
}

Request BackendCatalog::similarSongs(const QString& key, Reply<QList<SongInfo>> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(this, std::move(reply), Outcome<QList<SongInfo>>{QList<SongInfo>{}});
    return read<domain::DiscoveryBrowsePage>(
        m_requests,
        [key](BackendFacade& facade) {
            return facade.discover({domain::SimilarSongsQuery{{key}, {50, 0}}});
        },
        std::move(reply),
        [key](const domain::DiscoveryBrowsePage& page) {
            QList<SongInfo> songs;
            QStringList seen{key}; // the song itself, and repeats, are left out
            if (const auto* found = std::get_if<domain::DiscoverySongs>(&page.content))
                for (const auto& song : found->songs)
                    if (!seen.contains(song.id.value)) {
                        seen << song.id.value;
                        songs.append(songOf(song));
                    }
            return songs;
        });
}

Request BackendCatalog::similarPlaylists(const QString& key, Reply<QList<CollectionItem>> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(this, std::move(reply),
                          Outcome<QList<CollectionItem>>{QList<CollectionItem>{}});
    return read<domain::DiscoveryBrowsePage>(
        m_requests,
        [key](BackendFacade& facade) {
            return facade.discover({domain::SimilarPlaylistsQuery{{key}, {50, 0}}});
        },
        std::move(reply),
        [](const domain::DiscoveryBrowsePage& page) {
            QList<CollectionItem> playlists;
            QStringList seen;
            if (const auto* found = std::get_if<domain::DiscoveryPlaylists>(&page.content))
                for (const auto& playlist : found->playlists)
                    if (!seen.contains(playlist.id.value)) {
                        seen << playlist.id.value;
                        playlists.append(playlistOf(playlist));
                    }
            return playlists;
        });
}

Request BackendCatalog::podcast(const QString& key, Reply<PodcastInfo> reply)
{
    if (!domain::validMusicId(key))
        return replyLater(
            this, std::move(reply),
            Outcome<PodcastInfo>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
    return read<domain::PodcastDetails>(
        m_requests, [key](BackendFacade& facade) { return facade.podcastDetails({key}); },
        std::move(reply),
        [](const domain::PodcastDetails& details) {
            PodcastInfo info;
            info.key = details.podcast.id.value;
            info.title = details.podcast.title;
            info.description = details.podcast.description.value_or(QString());
            info.coverKey = coverKey(details.podcast.cover);
            info.voiceCount = details.podcast.voiceCount ? int(*details.podcast.voiceCount) : -1;
            return info;
        });
}

Request BackendCatalog::podcastVoices(const QString& key, bool oldestFirst, PageRequest page,
                                      Reply<ResultPage<SongInfo>> reply)
{
    const domain::PodcastProgramsQuery query{{key},
                                             {std::clamp(page.limit, 1, 100), page.offset},
                                             oldestFirst ? domain::ProgramOrder::OldestFirst
                                                         : domain::ProgramOrder::NewestFirst};
    if (!query.isValid())
        return replyLater(this, std::move(reply),
                          Outcome<ResultPage<SongInfo>>{ResultPage<SongInfo>{{}, false, {}, {}}});
    return read<domain::PodcastProgramsPage>(
        m_requests, [query](BackendFacade& facade) { return facade.podcastPrograms(query); },
        std::move(reply),
        [](const domain::PodcastProgramsPage& found) {
            return pageOf<domain::PodcastProgramsPage, SongInfo>(found, found.programs, programOf);
        });
}

Request BackendCatalog::dailySongs(Reply<QList<SongInfo>> reply)
{
    return readWithCovers<domain::DailyRecommendations>(
        m_requests,
        [](BackendFacade& facade) {
            return signedIn(facade) ? facade.dailyRecommendations() : MusicRequests::NeedsSignIn;
        },
        std::move(reply),
        [](const domain::DailyRecommendations& found) {
            QHash<QString, QString> reasons;
            for (const auto& reason : found.reasons)
                reasons.insert(reason.song.value, reason.text);
            QList<SongInfo> songs;
            songs.reserve(found.songs.size());
            for (const auto& recommended : found.songs) {
                SongInfo info = songOf(recommended.song);
                info.reason = recommended.inlineReason.value_or(reasons.value(info.key));
                songs.append(info);
            }
            return songs;
        },
        listSongs);
}

Request BackendCatalog::dailyHistoryDates(Reply<QStringList> reply)
{
    return read<domain::DailyHistoryDates>(
        m_requests,
        [](BackendFacade& facade) {
            return signedIn(facade) ? facade.dailyRecommendationHistoryDates()
                                    : MusicRequests::NeedsSignIn;
        },
        std::move(reply),
        [kept = m_historyDates](const domain::DailyHistoryDates& found) {
            *kept = std::make_shared<const domain::DailyHistoryDates>(found);
            QStringList dates;
            for (const QString& date : found.dates)
                if (!dates.contains(date)) // the service may repeat a day
                    dates.append(date);
            return dates;
        });
}

Request BackendCatalog::dailyHistorySongs(const QString& date, Reply<QList<SongInfo>> reply)
{
    return readWithCovers<domain::DailyHistorySongs>(
        m_requests,
        [kept = m_historyDates, date](BackendFacade& facade) -> quint64 {
            if (!signedIn(facade))
                return MusicRequests::NeedsSignIn;
            // Only a day from this account's last list (the page reads it first).
            const auto dates = *kept;
            if (!dates || dates->query.accountId != facade.snapshot().accountId)
                return MusicRequests::Refused;
            auto query =
                domain::DailyHistorySongsQuery::fromDates(facade.snapshot().token, *dates, date);
            if (!query)
                return MusicRequests::Refused;
            return facade.dailyRecommendationHistorySongs(std::move(*query));
        },
        std::move(reply),
        [](const domain::DailyHistorySongs& found) {
            QList<SongInfo> songs;
            songs.reserve(found.songs.size());
            for (const auto& song : found.songs)
                songs.append(songOf(song));
            return songs;
        },
        listSongs);
}

} // namespace cirrus::ui
