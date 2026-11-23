#pragma once
#include "catalog_types.h"
#include "entity_search_types.h"
#include "logging/log.h"
#include "music_types.h"
#include "playlist_cover_types.h"
#include "playlist_order_types.h"
#include "playlist_tag_edit_types.h"
#include <QSet>
#include <QUuid>
#include <variant>

namespace cirrus::domain
{
// Product wording: Collect adds songs to a selected playlist; heart is independent.
enum class CollectionAction { Collect, Remove };
struct SongHeart {
    SongId song;
    bool liked = true;
    [[nodiscard]] bool isValid() const { return validMusicId(song.value); }
};
struct PlaylistCollection {
    PlaylistId playlist;
    QList<SongId> songs;
    CollectionAction action = CollectionAction::Collect;
    [[nodiscard]] bool isValid() const
    {
        if (!validMusicId(playlist.value) || songs.isEmpty() || songs.size() > 100 ||
            (action != CollectionAction::Collect && action != CollectionAction::Remove))
            return false;
        QSet<QString> seen;
        for (const auto& id : songs) {
            if (!validMusicId(id.value) || seen.contains(id.value))
                return false;
            seen.insert(id.value);
        }
        return true;
    }
};
struct AlbumCollection {
    AlbumId album;
    CollectionAction action = CollectionAction::Collect;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(album.value) &&
               (action == CollectionAction::Collect || action == CollectionAction::Remove);
    }
};
struct ArtistCollection {
    ArtistId artist;
    CollectionAction action = CollectionAction::Collect;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(artist.value) &&
               (action == CollectionAction::Collect || action == CollectionAction::Remove);
    }
};
struct PodcastSubscription {
    PodcastId podcast;
    CollectionAction action = CollectionAction::Collect;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(podcast.value) &&
               (action == CollectionAction::Collect || action == CollectionAction::Remove);
    }
};
// Saves someone else's playlist to the account's library, or removes it (docs/api/
// playlist-subscription.md): not its songs (PlaylistCollection).
struct PlaylistSubscription {
    PlaylistId playlist;
    CollectionAction action = CollectionAction::Collect;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(playlist.value) &&
               (action == CollectionAction::Collect || action == CollectionAction::Remove);
    }
};
// Follows or unfollows a user (docs/api/user-profile-reads.md §5).
struct UserFollow {
    QString userId;
    CollectionAction action = CollectionAction::Collect;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(userId) &&
               (action == CollectionAction::Collect || action == CollectionAction::Remove);
    }
};
// Budgets are local UTF-16 limits, not claims about provider maxima. Never normalize text.
inline bool validPlaylistText(const QString& text, qsizetype maximum, bool description)
{
    if (text.size() > maximum || !text.isValidUtf16() || (!description && text.trimmed().isEmpty()))
        return false;
    for (const auto ch : text) {
        const auto n = ch.unicode();
        if (n == 0x7f || (n < 0x20 && !(description && (n == 9 || n == 10 || n == 13))))
            return false;
    }
    return true;
}
enum class PlaylistPrivacy { Public, Private };
struct PlaylistCreate {
    QString name;
    PlaylistPrivacy privacy = PlaylistPrivacy::Public;
    [[nodiscard]] bool isValid() const
    {
        return validPlaylistText(name, 128, false) &&
               (privacy == PlaylistPrivacy::Public || privacy == PlaylistPrivacy::Private);
    }
};
struct PlaylistRename {
    PlaylistId playlist;
    QString name;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(playlist.value) && validPlaylistText(name, 128, false);
    }
};
struct PlaylistDescription {
    PlaylistId playlist;
    QString description; // explicit empty text clears; not an omitted field/default update
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(playlist.value) && validPlaylistText(description, 4096, true);
    }
};
// No CloudSong/Program/Voice casts or JSON.
using CollectionChange =
    std::variant<SongHeart, PlaylistCollection, AlbumCollection, ArtistCollection,
                 PodcastSubscription, PlaylistCreate, PlaylistRename, PlaylistDescription,
                 PlaylistDelete, PlaylistSongOrder, AccountPlaylistOrder, PlaylistTagEdit,
                 PlaylistMakePublic, PlaylistCoverReplace, UserFollow, PlaylistSubscription>;
struct CollectionMutation {
    QUuid eventId; // local correlation only, NOT server idempotency; caller explicitly supplies it
    CollectionChange change;
    [[nodiscard]] bool isValid() const
    {
        return !eventId.isNull() && std::visit([](const auto& c) { return c.isValid(); }, change);
    }
};
inline std::optional<SessionToken> collectionSourceContext(const CollectionMutation& mutation)
{
    if (const auto* c = std::get_if<PlaylistTagEdit>(&mutation.change))
        return c->sourceContext();
    if (const auto* c = std::get_if<PlaylistSongOrder>(&mutation.change))
        return c->sourceContext();
    if (const auto* c = std::get_if<AccountPlaylistOrder>(&mutation.change))
        return c->sourceContext();
    return std::nullopt;
}
inline std::optional<QString> collectionSourceAccount(const CollectionMutation& mutation)
{
    if (const auto* c = std::get_if<AccountPlaylistOrder>(&mutation.change))
        return c->sourceAccount();
    return std::nullopt;
}
enum class CollectionOutcome { NotSent, Acknowledged, Rejected, Unknown };
CIRRUS_LOG_ENUM(CollectionOutcome, NotSent, Acknowledged, Rejected, Unknown)
enum class CollectionError {
    None,
    InvalidRequest,
    AuthenticationRequired,
    StaleContext,
    Busy,
    Canceled,
    Network,
    Timeout,
    RateLimited,
    InvalidResponse,
    ProviderRejected,
    ProtocolFailure
};
CIRRUS_LOG_ENUM(CollectionError, None, InvalidRequest, AuthenticationRequired, StaleContext, Busy,
                Canceled, Network, Timeout, RateLimited, InvalidResponse, ProviderRejected,
                ProtocolFailure)
struct CollectionResult {
    QUuid eventId;
    CollectionOutcome outcome = CollectionOutcome::NotSent;
    CollectionError error = CollectionError::None;
    std::optional<int> providerCode;
    std::optional<qint64> retryAfterMs;        // information only; no automatic write retry
    std::optional<PlaylistId> createdPlaylist; // only valid creation Ack in the original context
    std::optional<CoverStep> coverStep;        // last observed stage, not progress/effect proof
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::CollectionResult)
