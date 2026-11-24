#include "together_gateway.h"
#include "infrastructure/netease/api/together_operation.h"
#include "metadata_mapping.h"
#include "music_mapping.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>
#include <utility>

namespace cirrus::netease
{
using namespace domain;
namespace
{
bool absent(const QJsonValue& v) { return v.isUndefined() || v.isNull(); }
bool optionalBool(const QJsonValue& v, std::optional<bool>& out)
{
    if (absent(v))
        return true;
    if (!v.isBool())
        return false;
    out = v.toBool();
    return true;
}
// A song ID as text (or, tolerated, a JSON number). Absent, "" and "0" mean none.
bool optionalSong(const QJsonValue& v, SongId& out)
{
    if (absent(v) || v == QJsonValue(QString()) || v == QJsonValue(QStringLiteral("0")))
        return true;
    if (v.isDouble()) {
        const auto user = togetherUser(v);
        if (!user)
            return false;
        out = {*user};
        return true;
    }
    if (!v.isString() || !validMusicId(v.toString()))
        return false;
    out = {v.toString()};
    return true;
}
std::optional<TogetherRoom> roomFrom(const QJsonValue& v)
{
    if (!v.isObject())
        return std::nullopt;
    const auto o = v.toObject();
    const auto id = o.value(QStringLiteral("roomId"));
    if (!id.isString() || !TogetherRoomId{id.toString()}.isValid())
        return std::nullopt;
    TogetherRoom r{{id.toString()}};
    const auto chat = o.value(QStringLiteral("chatRoomId"));
    if (!absent(chat)) {
        if (!chat.isString() || !TogetherRoomId{chat.toString()}.isValid())
            return std::nullopt;
        r.chatRoom = TogetherChatRoomId{chat.toString()};
    }
    if (!mapping::metadataText(o.value(QStringLiteral("imType")), r.imType) ||
        !mapping::metadataText(o.value(QStringLiteral("roomType")), r.roomType) ||
        !optionalBool(o.value(QStringLiteral("mainDevice")), r.mainDevice) ||
        !optionalBool(o.value(QStringLiteral("roomCanJoin")), r.canJoin) ||
        !optionalBool(o.value(QStringLiteral("roomEnd")), r.ended))
        return std::nullopt;
    const auto creator = o.value(QStringLiteral("creatorId"));
    if (!absent(creator)) {
        const auto user = togetherUser(creator);
        if (!user)
            return std::nullopt;
        if (!user->isEmpty())
            r.creatorId = *user;
    }
    const auto users = o.value(QStringLiteral("roomUsers"));
    if (!absent(users)) {
        if (!users.isArray() || users.toArray().size() > 1000)
            return std::nullopt;
        for (const auto& u : users.toArray()) {
            const auto m = u.toObject();
            const auto user = togetherUser(m.value(QStringLiteral("userId")));
            if (!u.isObject() || !user || user->isEmpty())
                return std::nullopt;
            // Display only: a nickname this client cannot show stays empty.
            std::optional<QString> nickname;
            if (!mapping::metadataText(m.value(QStringLiteral("nickname")), nickname, 256))
                nickname.reset();
            r.members.append({*user, nickname.value_or(QString()),
                              mapping::imageFrom(m.value(QStringLiteral("avatarUrl")))});
        }
    }
    return r;
}
std::optional<TogetherReportedPlay> reportedPlay(const QJsonValue& v)
{
    if (!v.isObject())
        return std::nullopt;
    const auto o = v.toObject();
    TogetherReportedPlay p;
    const auto action = togetherPlayAction(o.value(QStringLiteral("commandType")));
    const auto status = togetherPlayStatus(o.value(QStringLiteral("playStatus")));
    const auto progress = togetherCounter(o.value(QStringLiteral("progress")));
    const auto client = togetherCounter(o.value(QStringLiteral("clientSeq")));
    const auto server = togetherCounter(o.value(QStringLiteral("serverSeq")));
    const auto userValue = o.value(QStringLiteral("userId"));
    const auto user =
        absent(userValue) ? std::optional<QString>(QString()) : togetherUser(userValue);
    const auto trigger = o.value(QStringLiteral("triggerType"));
    if (!action || !status || !progress || !validTogetherProgress(*progress) || !client ||
        !server || !user || !optionalSong(o.value(QStringLiteral("formerSongId")), p.former) ||
        !optionalSong(o.value(QStringLiteral("targetSongId")), p.target))
        return std::nullopt;
    if (!absent(trigger)) {
        p.trigger = togetherTrigger(trigger);
        if (!p.trigger)
            return std::nullopt;
    }
    p.action = *action;
    p.status = *status;
    p.progressMs = *progress;
    p.clientSeq = *client;
    p.serverSeq = *server;
    p.userId = *user;
    return p;
}
// displayList / randomList: {changed, result: [song IDs]}.
bool songList(const QJsonValue& v, QList<SongId>& out, bool& changed)
{
    if (absent(v))
        return true;
    if (!v.isObject())
        return false;
    const auto o = v.toObject();
    std::optional<bool> c;
    if (!optionalBool(o.value(QStringLiteral("changed")), c))
        return false;
    changed = c.value_or(false);
    const auto result = o.value(QStringLiteral("result"));
    if (absent(result))
        return true;
    if (!result.isArray() || result.toArray().size() > 5000)
        return false;
    for (const auto& id : result.toArray()) {
        SongId song;
        if (!optionalSong(id, song) || song.value.isEmpty())
            return false;
        out.append(song);
    }
    return true;
}
std::optional<TogetherPlaylist> playlistFrom(const QJsonValue& v)
{
    if (!v.isObject())
        return std::nullopt;
    const auto o = v.toObject();
    TogetherPlaylist p;
    std::optional<bool> replace;
    if (!songList(o.value(QStringLiteral("displayList")), p.display, p.displayChanged) ||
        !songList(o.value(QStringLiteral("randomList")), p.random, p.randomChanged) ||
        !optionalBool(o.value(QStringLiteral("replace")), replace))
        return std::nullopt;
    p.replace = replace.value_or(false);
    const auto mode = o.value(QStringLiteral("playMode"));
    if (!absent(mode)) {
        if (!mode.isString())
            return std::nullopt;
        p.mode = togetherPlayMode(mode); // a mode this client does not know stays empty
    }
    const auto versions = o.value(QStringLiteral("version"));
    if (!absent(versions)) {
        const auto list = togetherVersions(versions);
        if (!list)
            return std::nullopt;
        p.versions = *list;
    }
    return p;
}
// data.type and data.hintText (RoomInfoResult, HeartBeatResult); both optional.
bool typeAndHint(const QJsonObject& o, TogetherResult& r)
{
    return mapping::metadataText(o.value(QStringLiteral("type")), r.resultType, 128) &&
           mapping::metadataText(o.value(QStringLiteral("hintText")), r.hint);
}
bool details(const TogetherRequest& request, const QJsonObject& root, TogetherResult& r)
{
    const auto raw = root.value(QStringLiteral("data"));
    const auto kind = togetherKind(request.action);
    // Exits and notices: the envelope is the answer (end/v2's share card is not used).
    if (kind == TogetherKind::End || kind == TogetherKind::DeviceNotice ||
        kind == TogetherKind::SyncNotice) {
        if (absent(raw))
            return true;
        if (!raw.isObject())
            return false;
    }
    if (!raw.isObject())
        return false;
    const auto o = raw.toObject();
    if (kind == TogetherKind::Create || kind == TogetherKind::Join ||
        kind == TogetherKind::Status) {
        if (kind == TogetherKind::Status) {
            const auto in = o.value(QStringLiteral("inRoom"));
            if (!in.isBool())
                return false;
            r.inRoom = in.toBool();
            if (!*r.inRoom)
                return absent(o.value(QStringLiteral("roomInfo")));
        } else if (!typeAndHint(o, r))
            return false;
        r.room = roomFrom(o.value(QStringLiteral("roomInfo")));
        if (!r.room)
            return false;
        if (const auto* join = std::get_if<TogetherJoin>(&request.action))
            return join->room == r.room->id;
        return true;
    }
    if (kind == TogetherKind::Check || kind == TogetherKind::EndCheck ||
        kind == TogetherKind::Heartbeat) {
        const auto key =
            kind == TogetherKind::Check ? QStringLiteral("joinable") : QStringLiteral("result");
        const auto b = o.value(key);
        if (!b.isBool())
            return false;
        if (kind == TogetherKind::Check)
            r.joinable = b.toBool();
        else if (kind == TogetherKind::EndCheck)
            r.endAllowed = b.toBool();
        else {
            r.heartbeatAccepted = b.toBool();
            if (!typeAndHint(o, r))
                return false;
            const auto time = o.value(QStringLiteral("time"));
            if (!absent(time)) {
                const auto ms = togetherCounter(time);
                if (!ms)
                    return false;
                r.serverTimeMs = *ms;
            }
        }
        // A false decision is valid read metadata, not activation or transport failure.
        return true;
    }
    if (kind == TogetherKind::Queue) {
        // data is the app's LTPlayListWrapper: {playCommand, playlist}.
        r.playlist = playlistFrom(o.value(QStringLiteral("playlist")));
        if (!r.playlist)
            return false;
        const auto play = o.value(QStringLiteral("playCommand"));
        if (!absent(play)) {
            r.reportedPlay = reportedPlay(play);
            if (!r.reportedPlay) {
                r.playlist.reset();
                return false;
            }
        }
        return true;
    }
    if (kind == TogetherKind::ReconnectInfo) {
        const auto can = o.value(QStringLiteral("canReconnect"));
        if (!can.isBool())
            return false;
        TogetherReconnect c;
        c.canReconnect = can.toBool();
        const auto id = o.value(QStringLiteral("roomId"));
        if (!absent(id) && id != QJsonValue(QString())) {
            if (!id.isString() || !TogetherRoomId{id.toString()}.isValid())
                return false;
            c.room = TogetherRoomId{id.toString()};
        }
        if ((c.canReconnect && !c.room) ||
            !optionalBool(o.value(QStringLiteral("mainDevice")), c.mainDevice) ||
            !optionalBool(o.value(QStringLiteral("needDeviceReconnectConfirm")),
                          c.requiresDeviceConfirmation) ||
            !optionalBool(o.value(QStringLiteral("supportMultiDeviceReconnect")),
                          c.supportsMultiDevice))
            return false;
        r.reconnect = c;
        return true;
    }
    if (kind == TogetherKind::End)
        return true;
    // Play/list GeneralResult needs its boolean result; the notices have no established
    // result schema and only acknowledge the HTTP envelope.
    const auto result = o.value(QStringLiteral("result"));
    if ((kind == TogetherKind::Play || kind == TogetherKind::ReplaceQueue) && !result.isBool())
        return false;
    if (!result.isUndefined()) {
        if (!result.isBool())
            return false;
        if (!result.toBool()) {
            r.outcome = TogetherOutcome::Rejected;
            r.error = TogetherError::ProviderRejected;
        }
    }
    const auto innerCode = o.value(QStringLiteral("code"));
    if (!innerCode.isUndefined()) {
        const auto n = togetherCounter(innerCode);
        if (!n)
            return false;
        if (*n != 200) {
            r.outcome = TogetherOutcome::Rejected;
            r.error = TogetherError::ProviderRejected;
        }
    }
    return true;
}
} // namespace
std::optional<TogetherWireRequest> togetherRequest(const TogetherRequest& request,
                                                   QStringView account)
{
    if (!request.isValid() || !validMusicId(account))
        return std::nullopt;
    return TogetherWireRequest{togetherOperation(togetherKind(request.action)),
                               togetherParameters(request.action)};
}
TogetherResult togetherResult(const TogetherRequest& request, const ApiResult& api)
{
    TogetherResult r;
    r.eventId = request.eventId;
    r.kind = togetherKind(request.action);
    r.roomGeneration = request.roomGeneration;
    r.outcome = api.submittedToTransport ? TogetherOutcome::Unknown : TogetherOutcome::NotSent;
    if (api.error == ApiError::None && api.decoded.ok() && api.decoded.businessCode) {
        r.providerCode = api.decoded.businessCode;
        r.outcome =
            *r.providerCode == 200 ? TogetherOutcome::Acknowledged : TogetherOutcome::Rejected;
        r.error = *r.providerCode == 200 ? TogetherError::None : TogetherError::ProviderRejected;
        // A rejection may still say why (e.g. 488 with ERROR_ROOM_INVALID); ignore a bad one.
        if (*r.providerCode != 200 &&
            !typeAndHint(api.decoded.object.value(QStringLiteral("data")).toObject(), r))
            r.resultType = r.hint = std::nullopt;
        if (*r.providerCode == 200 && !details(request, api.decoded.object, r)) {
            r = TogetherResult{request.eventId,
                               TogetherOutcome::Unknown,
                               TogetherError::InvalidResponse,
                               togetherKind(request.action),
                               request.roomGeneration,
                               api.decoded.businessCode};
        }
    } else if (api.error == ApiError::Canceled)
        r.error = TogetherError::Canceled;
    else if (api.error == ApiError::StaleContext)
        r.error = TogetherError::StaleContext;
    else if (api.error == ApiError::Busy)
        r.error = TogetherError::Busy;
    else if (api.error == ApiError::InvalidRequest)
        r.error = TogetherError::InvalidRequest;
    else if (api.error == ApiError::Decode || api.error == ApiError::None)
        r.error = TogetherError::InvalidResponse;
    else if (api.transportError == TransportError::Timeout)
        r.error = TogetherError::Timeout;
    else if (api.error == ApiError::Transport)
        r.error = TogetherError::Network;
    else
        r.error = TogetherError::ProtocolFailure;
    if (api.httpStatus == 429 || api.decoded.businessCode == 429) {
        r.error = TogetherError::RateLimited;
        bool ok = false;
        const auto seconds = api.retryAfter.toLongLong(&ok);
        if (ok && seconds >= 0 && seconds <= 86400)
            r.retryAfterMs = seconds * 1000;
    }
    return r;
}
TogetherGateway::TogetherGateway(application::SessionCoordinator& session, SessionApiClient& client,
                                 QObject* parent)
    : ITogetherGateway(parent), m_session(session), m_client(&client)
{
    Q_ASSERT(session.thread() == thread() && client.thread() == thread());
    connect(
        &client, &SessionApiClient::completed, this,
        [this](quint64 apiId, SessionToken token, const ApiResult& api) {
            const auto id = m_apiIds.value(apiId);
            if (!m_pending.contains(id))
                return;
            const auto p = m_pending[id];
            auto r = togetherResult(p.request, api);
            if (token != p.context || !current(p) || p.forced != TogetherError::None) {
                r = {p.request.eventId,
                     api.submittedToTransport ? TogetherOutcome::Unknown : TogetherOutcome::NotSent,
                     !current(p) || token != p.context ? TogetherError::StaleContext : p.forced};
            }
            finish(id, r);
        });
    connect(&session, &application::SessionCoordinator::changed, this, [this](SessionSnapshot s) {
        for (auto id : m_pending.keys()) {
            const auto p = m_pending.value(id);
            if (s.token != p.context || s.phase != SessionPhase::Authenticated ||
                s.accountId != p.account || !current(p))
                cancelWith(id, TogetherError::StaleContext);
        }
    });
    connect(&client, &QObject::destroyed, this, [this] {
        for (auto id : m_pending.keys())
            finish(id, {{},
                        m_pending[id].apiId ? TogetherOutcome::Unknown : TogetherOutcome::NotSent,
                        TogetherError::Network});
    });
}
TogetherGateway::~TogetherGateway()
{
    if (!m_client)
        return;
    disconnect(m_client, nullptr, this, nullptr);
    for (auto apiId : m_apiIds.keys())
        m_client->cancel(apiId);
}
bool TogetherGateway::current(const Pending& p) const
{
    const auto s = m_session.snapshot();
    return m_client && m_session.accepts(p.context) && s.phase == SessionPhase::Authenticated &&
           p.account == s.accountId;
}
quint64 TogetherGateway::submit(SessionToken context, QString account, TogetherRequest request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 8)
        return 0;
    for (const auto& p : std::as_const(m_pending))
        if (!request.eventId.isNull() && p.request.eventId == request.eventId)
            return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id, {context, std::move(account), std::move(request)});
    QTimer::singleShot(0, this, [this, id] { start(id); });
    return id;
}
void TogetherGateway::start(quint64 id)
{
    if (!m_pending.contains(id))
        return;
    auto& p = m_pending[id];
    TogetherError error = p.forced;
    if (error == TogetherError::None) {
        if (!m_session.accepts(p.context))
            error = TogetherError::StaleContext;
        else if (m_session.snapshot().phase != SessionPhase::Authenticated)
            error = TogetherError::AuthenticationRequired;
        else if (!p.request.isValid() || !validMusicId(p.account) ||
                 p.account != m_session.snapshot().accountId)
            error = TogetherError::InvalidRequest;
        else if (!m_client)
            error = TogetherError::Network;
    }
    if (error != TogetherError::None) {
        finish(id, {{}, TogetherOutcome::NotSent, error});
        return;
    }
    const auto wire = togetherRequest(p.request, p.account);
    p.apiId = m_client->submit(p.context, wire->operation, wire->parameters);
    m_apiIds.insert(p.apiId, id);
}
void TogetherGateway::cancelWith(quint64 id, TogetherError error)
{
    if (!m_pending.contains(id))
        return;
    auto& p = m_pending[id];
    p.forced = error;
    if (p.apiId && m_client)
        m_client->cancel(p.apiId);
    else
        QTimer::singleShot(
            0, this, [this, id, error] { finish(id, {{}, TogetherOutcome::NotSent, error}); });
}
void TogetherGateway::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelWith(id, TogetherError::Canceled);
}
void TogetherGateway::finish(quint64 id, TogetherResult result)
{
    if (!m_pending.contains(id))
        return;
    const auto p = m_pending.take(id);
    m_apiIds.remove(p.apiId);
    result.eventId = p.request.eventId;
    result.kind = togetherKind(p.request.action);
    result.roomGeneration = p.request.roomGeneration;
    emit completed(id, p.context, result);
}
} // namespace cirrus::netease
