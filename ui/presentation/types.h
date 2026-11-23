#pragma once

#include "collection.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Display values the data sources hand to ViewModels. Defined by the UI rather than
// reusing backend domain types, so fakes need no backend and QML never sees DTOs
// (docs/ui/presentation.md §2). Keys are opaque strings (§4.3).
namespace cirrus::ui
{

struct ArtistRef {
    QString key; // may be empty
    QString name;
};

struct SongInfo {
    QString key;
    QString title;
    QString secondaryTitle; // alias or translated title, may be empty
    QList<ArtistRef> artists;
    QString albumKey;
    QString albumTitle;
    QString coverKey;
    qint64 durationMs = -1;   // -1 when unknown
    qint64 publishedAtMs = 0; // 0 when unknown
    bool unavailable = false;
    // Why it was recommended (daily songs), as the service words it; may be empty.
    QString reason;
    // A podcast's voice (episode) instead of a song: albumKey and albumTitle are the
    // podcast's, `serial` its episode number (-1 when unknown).
    bool voice = false;
    int serial = -1;
    // A voice's audio: the song the service plays for it (its key); empty when unknown,
    // and for songs.
    QString trackKey;
};

struct PlaylistInfo {
    QString key;
    QString title;
    QString description;
    QString creator;
    QString creatorKey; // the creator's user page, empty when unknown
    QString coverKey;
    QStringList tags;
    qint64 updatedAtMs = 0; // 0 when unknown
    // -1 when unknown.
    qint64 playCount = -1;
    qint64 subscriberCount = -1;
    qint64 commentCount = -1;
    // Created by the signed-in account; it may edit the playlist.
    bool own = false;
    // Saved by the signed-in account (someone else's playlist).
    bool subscribed = false;
    // The account's "我喜欢的音乐": a playlist like any other, except that its title
    // and description can't be edited and it can't be deleted.
    bool liked = false;
};

// Most song keys one CatalogSource::songDetails() request takes (the service's limit).
inline constexpr int kSongDetailsBatch = 100;

// A playlist's header and tracks. `songs` may hold details for only some of
// `songKeys` (large playlists); the rest are fetched as they scroll into view.
struct PlaylistPage {
    PlaylistInfo info;
    QStringList songKeys;
    QList<SongInfo> songs;
};

struct AlbumInfo {
    QString key;
    QString title;
    QList<ArtistRef> artists;
    int year = 0; // 0 when unknown
    QString coverKey;
    QString description;
    // The service's wording: 专辑, EP, 单曲…; empty when unknown.
    QString type;
    QStringList aliases;
    qint64 publishedAtMs = 0; // 0 when unknown
    QString company;          // record label, empty when unknown
    qint64 commentCount = -1; // -1 when unknown
    // Saved by the signed-in account.
    bool subscribed = false;
};

struct AlbumPage {
    AlbumInfo info;
    QStringList songKeys;
    QList<SongInfo> songs;
};

struct ArtistInfo {
    QString key;
    QString name;
    QString description;
    QString avatarKey;   // a cover key, may be empty
    int songCount = -1;  // -1 when unknown
    int albumCount = -1; // -1 when unknown
    // Other names and translations.
    QStringList aliases;
    // The musician's own user page, empty when unknown.
    QString userKey;
    // Followed by the signed-in account.
    bool followed = false;
};

// An artist's introduction: a brief, then titled sections of plain text.
struct ArtistIntroduction {
    QString brief;
    struct Section {
        QString title; // may be empty
        QString text;
    };
    QList<Section> sections;
};

// A song's wiki as the song page shows it (docs/ui/pages/song.md §2): tag cards (style,
// language, BPM: the service's display text) and titled sections of label / text rows.
// Plain text only; the service's content, not checked to be complete.
struct SongWiki {
    struct Card {
        QString title;
        QStringList values;
    };
    struct Row {
        QString label;
        QString text; // several short texts are joined with " / "
    };
    struct Section {
        QString title;
        QString description; // may be empty
        QList<Row> rows;
    };
    QList<Card> cards;
    QList<Section> sections;
};

// A playlist, album or artist as a card. Raw fields; the list model words the subtitle.
struct CollectionItem {
    QString key;
    collection::Kind kind = collection::Playlist;
    QString title;
    QString byline; // album artists or playlist creator
    int year = 0;   // albums, 0 when unknown
    int count = -1; // songs (playlists, albums) or albums (artists), -1 when unknown
    QString coverKey;
    // A playlist of the account's library: its own (true) or saved (false), when known.
    std::optional<bool> created;
    // Playlists: how often played, as the service counts it; -1 when unknown.
    qint64 playCount = -1;
};

// An artist's header, popular songs and albums.
// The artist's header and popular songs (at most 50); songs and albums come in pages.
struct ArtistPage {
    ArtistInfo info;
    QList<SongInfo> topSongs;
};

// The signed-in account's collections.
struct LibraryPage {
    QList<SongInfo> likedSongs;
    QList<CollectionItem> createdPlaylists;
    QList<CollectionItem> savedPlaylists;
    QList<CollectionItem> albums;
    QList<CollectionItem> artists;
    QList<SongInfo> recentSongs;
};

namespace feed
{
Q_NAMESPACE
QML_NAMED_ELEMENT(Feed)
enum Which { Home, Discover };
Q_ENUM_NS(Which)
// Shortcuts: the daily songs and personal FM cards.
enum SectionKind { Shortcuts, Collections, Songs };
Q_ENUM_NS(SectionKind)
// Card rows of the home page: today's playlists (signed in), recommended playlists
// (anyone), the newest albums.
enum Shelf { DailyPlaylists, RecommendedPlaylists, NewAlbums, PopularArtists };
Q_ENUM_NS(Shelf)
} // namespace feed
using Feed = feed::Which;

// One section of the home or discover page: a row of cards or a few songs.
struct FeedSection {
    feed::SectionKind kind = feed::Collections;
    QString title;
    QList<CollectionItem> collections;
    QList<SongInfo> songs;
};

// A chart: a playlist, with its first few songs as the service previews them (text
// only, not songs).
struct ChartPreview {
    QString title;
    QString subtitle; // may be empty
};

struct ChartInfo {
    QString key; // a playlist key
    QString title;
    QString updateLabel; // e.g. "每天更新", as the service words it; may be empty
    QString coverKey;
    QList<ChartPreview> previews;
};

// One page of an offset-paged list (search results).
struct PageRequest {
    int offset = 0;
    int limit = 30;
};

template <class T> struct ResultPage {
    QList<T> items;
    // Unknown when the source can't tell: treated as "maybe", until an empty page.
    std::optional<bool> hasMore;
    // All results, when the source tells.
    std::optional<qint64> total;
    // Cursor-paged lists (premium playlists): where the next page starts; none at the end.
    std::optional<qint64> next;
};

// A user's page (docs/ui/pages/user.md): any account, a musician's too.
struct UserInfo {
    QString key;
    QString nickname;
    QString avatarKey; // a cover key, may be empty
    QString signature;
    int level = -1;          // -1 when unknown
    qint64 follows = -1;     // -1 when unknown
    qint64 fans = -1;        // -1 when unknown
    qint64 playlists = -1;   // -1 when unknown
    QString artistKey;       // a musician's artist page; empty for others
    bool rankingOpen = true; // others may see the listening ranking
    // Followed by the signed-in account.
    bool followed = false;
};

// A row of a user's follows or fans.
struct UserItem {
    QString key;
    QString nickname;
    QString avatarKey;
    QString signature;
    bool followed = false; // by the signed-in account
};

// A page of follows or fans, or `hidden` when the user doesn't show them.
struct UserList {
    ResultPage<UserItem> page;
    bool hidden = false;
};

// A user's listening ranking, most played first, or `hidden` when not shown to others.
struct UserRanking {
    QList<SongInfo> songs;
    bool hidden = false;
};

// Playlist categories for discover (docs/ui/pages/discover.md §3.1): the trending ones,
// all of them by group (语种, 风格…), and those that have premium playlists.
struct PlaylistCategories {
    struct Group {
        QString name;
        QStringList categories;
    };
    QStringList hot;
    QList<Group> groups;
    QStringList premium;
};

// What the search field suggests while typing: a song (searched by its title) or an
// artist, album or playlist (opened).
struct SearchSuggestion {
    bool song = false;
    collection::Kind kind = collection::Playlist; // when not a song
    QString key;
    QString title;
};

// A trending search, with the service's note (may be empty).
struct HotKeyword {
    QString keyword;
    QString description;
};

struct PodcastInfo {
    QString key;
    QString title;
    QString description;
    QString coverKey;
    int voiceCount = -1; // -1 when unknown
    // Saved by the signed-in account.
    bool subscribed = false;
};

// Comments (docs/ui/pages/comments.md): of a song, playlist or album.
namespace comment
{
enum Target { Song, Playlist, Album };
// Recommended mixes popular and recent ones, as the service orders them.
enum Sort { Recommended, Hot, Newest };
} // namespace comment

struct CommentTarget {
    comment::Target kind = comment::Song;
    QString key;
};

// The comment one replies to, as the service quotes it; empty text when it was deleted.
struct CommentExcerpt {
    QString authorName;
    QString text;
};

// One comment or reply. The text is untrusted plain text: shown as is, no links.
struct CommentInfo {
    QString key;
    QString authorKey; // the author's account; compares with the session's accountKey
    QString authorName;
    QString avatarKey;           // may be empty
    std::optional<QString> text; // none: deleted or under review
    qint64 timeMs = 0;           // 0 when unknown
    qint64 likedCount = -1;      // -1 when unknown
    bool liked = false;          // by the signed-in account
    std::optional<CommentExcerpt> replyTo;
    qint64 replyCount = -1; // the replies under it (its floor); -1 when unknown
};

// A page of comments or replies. `next` goes back to the source for the next page
// (empty at the end); what it holds is the source's business.
struct CommentPage {
    QList<CommentInfo> items;
    std::optional<bool> hasMore;
    std::optional<qint64> total;
    QString next;
};

// The resource cache (covers, song details, lyrics): what it holds and may hold.
struct CacheUsage {
    qint64 bytes = 0;
    qint64 limitBytes = 0;
};

// A timed piece of a lyric line: a word, a character or a syllable.
struct TimedText {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
};

// Lyrics as the provider has them (docs/api/music-queries-and-lyrics.md §3): three
// languages, each in a standard and an enhanced version, kept apart; CurrentLyrics
// (App.lyrics) picks and aligns them. Times are on the song's clock.
struct LyricLine {
    qint64 startMs = -1; // -1 for plain text
    qint64 endMs = -1;   // -1 when unknown (the last line)
    QString text;
    QList<TimedText> words; // with Words timing
};
struct LyricTrack {
    enum Timing { PlainText, Lines, Words };
    Timing timing = Lines;
    QList<LyricLine> lines;
    qint64 offsetMs = 0; // positive: shown later
};
struct LanguageLyrics {
    std::optional<LyricTrack> standard; // lrc, tlyric, romalrc
    std::optional<LyricTrack> enhanced; // yrc, ytlrc, yromalrc (not always Words)
};
struct SongLyrics {
    LanguageLyrics original;
    LanguageLyrics translation;
    LanguageLyrics romanization;
    bool noLyric = false;      // the provider says there are none
    bool instrumental = false; // the provider says it is instrumental
};

} // namespace cirrus::ui
