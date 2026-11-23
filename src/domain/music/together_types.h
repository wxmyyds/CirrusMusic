#pragma once
#include "domain/music/music_types.h"
#include "logging/log.h"
#include <QList>
#include <QMetaType>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <optional>
#include <type_traits>
#include <variant>

// Listen-together HTTP requests and results, shaped after the official Android app 9.6.22
// (docs/api/together-polling.md).
namespace cirrus::domain
{
struct TogetherRoomId {
    QString value;
    bool isValid() const
    {
        if (value.isEmpty() || value.size() > 128)
            return false;
        for (auto c : value)
            if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') ||
                  (c >= u'0' && c <= u'9') || c == u'_' || c == u'-'))
                return false;
        return true;
    }
    friend bool operator==(const TogetherRoomId&, const TogetherRoomId&) = default;
};
// Physical chat-room metadata, NOT an IM credential or business room identifier.
struct TogetherChatRoomId {
    QString value;
};
inline constexpr qint64 kTogetherCounterMax = 9007199254740991LL; // exact in a JSON number
inline bool validTogetherCounter(qint64 n) { return n >= 0 && n <= kTogetherCounterMax; }
inline bool validTogetherProgress(qint64 n) { return n >= 0 && n <= 604800000; }
// A user ID the service sends as a JSON number (CommandVersion, PlayCommand, roomUsers).
inline bool validTogetherUser(QStringView id)
{
    return validMusicId(id) && id.toLongLong() <= kTogetherCounterMax;
}
inline constexpr qsizetype kTogetherSongsMax = 1000; // songs a request may carry
enum class TogetherPlayStatus { Play, Pause };
enum class TogetherPlayAction { Play, Pause, Next, Previous, Goto, Progress };
enum class TogetherPlayMode { OrderLoop, Random, SingleLoop };
// AUTO: the player moved by itself (song ended, audio focus); MANUAL: the user did it.
enum class TogetherTrigger { Auto, Manual };
// One member's playlist-command counter; the service hands the list back with the playlist.
struct TogetherVersion {
    QString userId;
    qint64 version = 0;
    friend bool operator==(const TogetherVersion&, const TogetherVersion&) = default;
};
// A client's own list as it describes it to the service.
struct TogetherList {
    SongId anchor;              // the song it plays; empty: none
    qint64 anchorPosition = -1; // its index in display; -1: none
    TogetherPlayMode mode = TogetherPlayMode::OrderLoop;
    QList<SongId> display, random; // random: empty, or the shuffled order of display
};
struct TogetherCreate {};
struct TogetherCheck {
    TogetherRoomId room;
};
struct TogetherJoin {
    TogetherRoomId room;
    QString inviterId;
};
struct TogetherStatus {};
struct TogetherEndCheck {
    TogetherRoomId room;
};
struct TogetherEnd {
    TogetherRoomId room;
};
struct TogetherHeartbeat {
    TogetherRoomId room;
    SongId song;
    TogetherPlayStatus status{};
    qint64 progressMs = 0;
    QList<TogetherVersion> versions; // the last list the service returned, unchanged
};
struct TogetherPlay {
    TogetherRoomId room;
    TogetherPlayAction action{};
    SongId former, target;
    TogetherPlayStatus status{};
    qint64 progressMs = 0, clientSeq = 0;
    TogetherTrigger trigger = TogetherTrigger::Manual;
};
// sync/playlist/get: reads the room's list and last play command, describing the own list.
struct TogetherQueue {
    TogetherRoomId room;
    TogetherList list;
};
// REPLACE only. Versions: the last list with the own counter raised by one (or added at 0).
struct TogetherReplaceQueue {
    TogetherRoomId room;
    TogetherList list;
    QList<TogetherVersion> versions;
    qint64 clientSeq = 0;
};
struct TogetherReconnectInfo {};
struct TogetherDeviceNotice {
    TogetherRoomId room;
};
// sync/notice: tells the service this client has applied the room's list.
struct TogetherSyncNotice {
    TogetherRoomId room;
};
using TogetherAction =
    std::variant<TogetherCreate, TogetherCheck, TogetherJoin, TogetherStatus, TogetherEndCheck,
                 TogetherEnd, TogetherHeartbeat, TogetherPlay, TogetherQueue, TogetherReplaceQueue,
                 TogetherReconnectInfo, TogetherDeviceNotice, TogetherSyncNotice>;
enum class TogetherKind {
    Create,
    Check,
    Join,
    Status,
    EndCheck,
    End,
    Heartbeat,
    Play,
    Queue,
    ReplaceQueue,
    ReconnectInfo,
    DeviceNotice,
    SyncNotice
};
inline constexpr int kTogetherKinds = int(TogetherKind::SyncNotice) + 1;
CIRRUS_LOG_ENUM(TogetherKind, Create, Check, Join, Status, EndCheck, End, Heartbeat, Play, Queue,
                ReplaceQueue, ReconnectInfo, DeviceNotice, SyncNotice)
CIRRUS_LOG_ENUM(TogetherPlayAction, Play, Pause, Next, Previous, Goto, Progress)
inline TogetherKind togetherKind(const TogetherAction& a) { return TogetherKind(a.index()); }
inline bool validTogetherSongs(const QList<SongId>& songs)
{
    return songs.size() <= kTogetherSongsMax &&
           std::all_of(songs.begin(), songs.end(),
                       [](const SongId& s) { return validMusicId(s.value); });
}
inline bool validTogetherList(const TogetherList& l)
{
    if ((!l.anchor.value.isEmpty() && !validMusicId(l.anchor.value)) || l.anchorPosition < -1 ||
        l.anchorPosition >= std::max<qsizetype>(l.display.size(), 1) ||
        l.mode < TogetherPlayMode::OrderLoop || l.mode > TogetherPlayMode::SingleLoop ||
        !validTogetherSongs(l.display) || !validTogetherSongs(l.random))
        return false;
    if (l.random.isEmpty())
        return true;
    if (l.random.size() != l.display.size())
        return false;
    QList<QString> d, r;
    for (const auto& id : l.display)
        d.append(id.value);
    for (const auto& id : l.random)
        r.append(id.value);
    std::sort(d.begin(), d.end());
    std::sort(r.begin(), r.end());
    return d == r;
}
inline bool validTogetherVersions(const QList<TogetherVersion>& versions)
{
    if (versions.size() > 64)
        return false;
    QList<QString> users;
    for (const auto& v : versions) {
        if (!validTogetherUser(v.userId) || !validTogetherCounter(v.version) ||
            users.contains(v.userId))
            return false;
        users.append(v.userId);
    }
    return true;
}
inline bool validTogetherAction(const TogetherAction& action)
{
    return std::visit(
        [](const auto& a) {
            using A = std::decay_t<decltype(a)>;
            if constexpr (requires { a.room; })
                if (!a.room.isValid())
                    return false;
            if constexpr (requires { a.inviterId; })
                if (!validMusicId(a.inviterId))
                    return false;
            if constexpr (requires { a.progressMs; })
                if (!validTogetherProgress(a.progressMs) || a.status < TogetherPlayStatus::Play ||
                    a.status > TogetherPlayStatus::Pause)
                    return false;
            if constexpr (requires { a.song; })
                if (!validMusicId(a.song.value))
                    return false;
            if constexpr (requires { a.former; })
                if (!validTogetherCounter(a.clientSeq) || !validMusicId(a.former.value) ||
                    !validMusicId(a.target.value) || a.action < TogetherPlayAction::Play ||
                    a.action > TogetherPlayAction::Progress || a.trigger < TogetherTrigger::Auto ||
                    a.trigger > TogetherTrigger::Manual)
                    return false;
            if constexpr (requires { a.versions; })
                if (!validTogetherVersions(a.versions))
                    return false;
            if constexpr (requires { a.list; })
                if (!validTogetherList(a.list))
                    return false;
            if constexpr (std::is_same_v<A, TogetherReplaceQueue>)
                if (!validTogetherCounter(a.clientSeq) || a.list.display.isEmpty() ||
                    a.versions.isEmpty())
                    return false;
            return true;
        },
        action);
}
struct TogetherRequest {
    QUuid eventId = QUuid::createUuid();
    quint64 roomGeneration = 0; // caller's local correlation; no automatic room transition fence
    TogetherAction action;
    bool isValid() const
    {
        return !eventId.isNull() && roomGeneration && validTogetherAction(action);
    }
};
enum class TogetherOutcome { NotSent, Acknowledged, Rejected, Unknown };
CIRRUS_LOG_ENUM(TogetherOutcome, NotSent, Acknowledged, Rejected, Unknown)
enum class TogetherError {
    None,
    InvalidRequest,
    AuthenticationRequired,
    StaleContext,
    Canceled,
    Busy,
    InvalidResponse,
    ProviderRejected,
    Timeout,
    Network,
    ProtocolFailure,
    RateLimited
};
CIRRUS_LOG_ENUM(TogetherError, None, InvalidRequest, AuthenticationRequired, StaleContext, Canceled,
                Busy, InvalidResponse, ProviderRejected, Timeout, Network, ProtocolFailure,
                RateLimited)
// A member as roomUsers lists them; nickname and avatar for display only.
struct TogetherMember {
    QString userId;
    QString nickname;
    QUrl avatar;
    friend bool operator==(const TogetherMember&, const TogetherMember&) = default;
};
struct TogetherRoom {
    TogetherRoomId id;
    std::optional<TogetherChatRoomId> chatRoom;
    std::optional<QString> imType, roomType, creatorId;
    QList<TogetherMember> members; // roomUsers, in the service's order
    std::optional<bool> mainDevice, canJoin, ended;
};
// The service's last play command in the room (sync/playlist/get).
struct TogetherReportedPlay {
    TogetherPlayAction action{};
    TogetherPlayStatus status{};
    SongId former, target; // empty when the service has none
    qint64 progressMs = 0, clientSeq = 0, serverSeq = 0;
    QString userId; // the member who sent it; empty when the service has none
    std::optional<TogetherTrigger> trigger;
};
// The room's list (sync/playlist/get).
struct TogetherPlaylist {
    QList<SongId> display, random;
    bool displayChanged = false, randomChanged = false, replace = false;
    std::optional<TogetherPlayMode> mode; // empty: none or a mode this client does not know
    QList<TogetherVersion> versions;
};
struct TogetherReconnect {
    bool canReconnect = false;
    std::optional<TogetherRoomId> room;
    std::optional<bool> mainDevice, requiresDeviceConfirmation, supportsMultiDevice;
};
struct TogetherResult {
    QUuid eventId;
    TogetherOutcome outcome = TogetherOutcome::NotSent;
    TogetherError error{};
    TogetherKind kind{};
    quint64 roomGeneration = 0;
    std::optional<qint64> providerCode, retryAfterMs;
    // data.type: Create/Join (e.g. NEW_ROOM, ALREADY_IN_ROOM), Heartbeat (ERROR_ROOM_INVALID);
    // also read from a rejection's data. hint: the service's hintText, for display only.
    std::optional<QString> resultType, hint;
    std::optional<TogetherRoom> room;
    std::optional<bool> inRoom, joinable, endAllowed, heartbeatAccepted;
    // Heartbeat: data.time, the service clock in ms since the epoch (2026-11-22 observed).
    std::optional<qint64> serverTimeMs;
    std::optional<TogetherReportedPlay> reportedPlay;
    std::optional<TogetherPlaylist> playlist;
    std::optional<TogetherReconnect> reconnect;
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::TogetherRequest)
Q_DECLARE_METATYPE(cirrus::domain::TogetherResult)
