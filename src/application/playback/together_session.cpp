#include "together_session.h"
#include "listening_session.h"
#include "logging/log.h"
#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

namespace cirrus::application
{
using namespace domain;
namespace
{
constexpr qint64 kProgressMax = 604800000;
constexpr qint64 kRoomEndedCode = 488; // sync/playlist/get and heartbeat once the room is over
const auto kShareLink = QStringLiteral("https://st.music.163.com/listen-together/share/index.html");

bool playing(const PlaybackSnapshot& s) { return s.intent == TransportIntent::Play; }
QList<SongId> effective(const QList<SongId>& display, const QList<SongId>& random,
                        TogetherPlayMode mode)
{
    return mode == TogetherPlayMode::Random && !random.isEmpty() ? random : display;
}
} // namespace

std::optional<TogetherSession::Invite> TogetherSession::parseInvite(const QString& text)
{
    // The app copies the link inside a sentence ("…一起听歌吧～ https://…&songId=… (@网易云音乐)"):
    // each http(s) URL in the text, up to the first character a share link does not use.
    static const QRegularExpression kLink(
        QStringLiteral(R"(https?://[A-Za-z0-9\-._~:/?#@!$&*+,;=%]+)"));
    if (text.size() > 4096)
        return std::nullopt;
    for (auto it = kLink.globalMatch(text); it.hasNext();) {
        auto link = it.next().captured();
        while (!link.isEmpty() && QStringView(u".,;:!?").contains(link.back()))
            link.chop(1);
        const QUrl url(link, QUrl::StrictMode);
        if (!url.isValid())
            continue;
        const QUrlQuery q(url);
        Invite i{{q.queryItemValue(QStringLiteral("roomId"))},
                 q.queryItemValue(QStringLiteral("inviterId"))};
        if (i.room.isValid() && validTogetherUser(i.inviterId))
            return i;
    }
    return std::nullopt;
}

TogetherSession::TogetherSession(BackendFacade& facade, ListeningSession& player, QObject* parent)
    : QObject(parent), m_facade(facade), m_player(player),
      m_quality([] { return AudioQuality::Standard; }),
      m_clock([] { return QDateTime::currentMSecsSinceEpoch(); })
{
    connect(&m_facade, &BackendFacade::togetherCompleted, this, &TogetherSession::completed);
    connect(&m_facade, &BackendFacade::sessionChanged, this, &TogetherSession::sessionChanged);
    connect(&m_player, &ListeningSession::playbackChanged, this, &TogetherSession::playbackChanged);
    connect(&m_player, &ListeningSession::queueChanged, this, [this] { queueChanged(); });
    connect(&m_facade, &BackendFacade::playbackSourceSelected, this,
            [this](PlaybackSourceSelection source) {
                if (m_state != State::Idle && source.provider == SourceProvider::Bilibili)
                    end(Reason::UnsupportedSource, true);
            });
    for (auto* t : {&m_reportTimer, &m_replaceTimer, &m_leaveTimer})
        t->setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &TogetherSession::poll);
    connect(&m_statusTimer, &QTimer::timeout, this, &TogetherSession::status);
    connect(&m_beatTimer, &QTimer::timeout, this, &TogetherSession::heartbeat);
    connect(&m_reportTimer, &QTimer::timeout, this, &TogetherSession::sendLocalPlay);
    connect(&m_replaceTimer, &QTimer::timeout, this, &TogetherSession::sendReplace);
    connect(&m_leaveTimer, &QTimer::timeout, this, [this] {
        if (m_state == State::Leaving)
            end(Reason::Left, false);
    });
    m_local = m_player.playback();
}

TogetherSession::~TogetherSession()
{
    m_player.setBiliSourcePermitted(true);
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        m_facade.cancelTogether(it.key());
}

// ---- requests ----

quint64 TogetherSession::send(TogetherAction action, Handler then)
{
    const auto kind = togetherKind(action);
    TogetherRequest request{QUuid::createUuid(), m_generation, std::move(action)};
    const auto id = request.isValid() ? m_facade.requestTogether(std::move(request)) : 0;
    if (!id) {
        log::warn(lcTogether, "together.not_sent", {"kind", kind});
        return 0;
    }
    m_pending.insert(id, std::move(then));
    return id;
}

void TogetherSession::completed(quint64 id, SessionToken context, const TogetherResult& r)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    auto then = std::move(it.value());
    m_pending.erase(it);
    if (context != m_context || r.roomGeneration != m_generation)
        return;
    if (r.outcome != TogetherOutcome::Acknowledged)
        log::debug(lcTogether, "together.result",
                   {{"kind", r.kind},
                    {"outcome", r.outcome},
                    {"error", r.error},
                    log::fieldIf(r.providerCode.has_value(), "code", r.providerCode.value_or(0))});
    if (r.hint && !r.hint->isEmpty() && r.outcome != TogetherOutcome::Acknowledged)
        m_hint = *r.hint;
    then(r);
}

void TogetherSession::sessionChanged(const SessionSnapshot& snapshot)
{
    if (m_state != State::Idle && snapshot.token != m_context)
        end(Reason::SignedOut, false);
}

// ---- starting ----

bool TogetherSession::supportedLocalPlayback() const
{
    const auto queue = m_player.queue();
    const auto selected = m_facade.playbackSource(m_player.playback().token.generation);
    return m_player.playback().provider != SourceProvider::Bilibili &&
           (!selected || selected->provider != SourceProvider::Bilibili) &&
           std::none_of(queue.entries.cbegin(), queue.entries.cend(),
                        [](const QueueEntry& entry) { return !entry.program.value.isEmpty(); });
}

bool TogetherSession::canCreate() const
{
    const auto s = m_facade.snapshot();
    return m_state == State::Idle && m_facade.state() == BackendState::Ready &&
           s.phase == SessionPhase::Authenticated && validTogetherUser(s.accountId) &&
           supportedLocalPlayback() && !localList().display.isEmpty();
}

void TogetherSession::begin()
{
    const auto s = m_facade.snapshot();
    ++m_generation;
    m_context = s.token;
    m_me = s.accountId;
    m_state = State::Starting;
    m_resuming = false;
    m_room = {};
    m_creator.clear();
    m_hint.clear();
    m_members.clear();
    m_display.clear();
    m_random.clear();
    m_versions.clear();
    m_mode = TogetherPlayMode::OrderLoop;
    m_serverSeq = -1;
    m_seq = 0;
    m_offset.reset();
    m_expect.reset();
    m_seek.reset();
    m_out.reset();
    m_afterReplace.reset();
    m_connected = true;
    m_local = m_player.playback();
    m_forceRoomReload = m_facade.allowBiliSource();
    m_player.setBiliSourcePermitted(false);
}

bool TogetherSession::create()
{
    if (!canCreate())
        return false;
    begin();
    m_joining = false;
    log::info(lcTogether, "together.start", {"join", false});
    emit changed();
    // A room left over (a crash, another device) would refuse the new one: end it first.
    const auto sent = send(TogetherStatus{}, [this](const TogetherResult& r) {
        if (r.outcome == TogetherOutcome::Acknowledged && r.inRoom == true && r.room &&
            send(TogetherEnd{r.room->id}, [this](const TogetherResult&) { createRoom(); }))
            return;
        createRoom();
    });
    if (!sent)
        end(Reason::Failed, false);
    return m_state == State::Starting;
}

void TogetherSession::createRoom()
{
    if (m_state != State::Starting)
        return;
    const auto sent = send(TogetherCreate{}, [this](const TogetherResult& r) {
        if (r.outcome != TogetherOutcome::Acknowledged || !r.room) {
            end(r.outcome == TogetherOutcome::Rejected ? Reason::Rejected : Reason::Failed, false);
            return;
        }
        m_room = r.room->id;
        m_creator = r.room->creatorId.value_or(m_me);
        m_members = r.room->members;
        replaceInitial();
    });
    if (!sent)
        end(Reason::Failed, false);
}

void TogetherSession::replaceInitial()
{
    if (!supportedLocalPlayback()) {
        end(Reason::UnsupportedSource, true);
        return;
    }
    const auto list = localList();
    if (m_forceRoomReload && !list.display.isEmpty()) {
        m_forceRoomReload = false;
        const TogetherReportedPlay play{
            .status = playing(m_local) ? TogetherPlayStatus::Play : TogetherPlayStatus::Pause,
            .target = list.anchor,
            .progressMs = m_local.positionMs};
        rebuild(effective(list.display, list.random, list.mode), list.mode, &play);
        m_seek = PendingSeek{list.anchor, play.progressMs, now(), playing(m_local)};
    }
    if (list.display.isEmpty()) {
        end(Reason::EmptyRoom, true);
        return;
    }
    m_display = list.display;
    m_random = list.random;
    m_mode = list.mode;
    m_versions = {{m_me, 0}};
    const auto sent = send(
        TogetherReplaceQueue{m_room, list, m_versions, m_seq++},
        [this, anchor = list.anchor](const TogetherResult& r) {
            if (m_state != State::Starting || !supportedLocalPlayback())
                return;
            if (r.outcome != TogetherOutcome::Acknowledged) {
                end(r.outcome == TogetherOutcome::Rejected ? Reason::Rejected : Reason::Failed,
                    true);
                return;
            }
            // The song playing now, as the app reports it to a member who asks.
            const auto progress = m_local.song == anchor ? m_local.positionMs : 0;
            const bool on = playing(m_local);
            sendPlay({m_room, on ? TogetherPlayAction::Play : TogetherPlayAction::Pause, anchor,
                      anchor, on ? TogetherPlayStatus::Play : TogetherPlayStatus::Pause,
                      std::clamp<qint64>(progress, 0, kProgressMax), 0, TogetherTrigger::Manual});
            activate();
        });
    if (!sent)
        end(Reason::Failed, true);
}

bool TogetherSession::join(const Invite& invite)
{
    const auto s = m_facade.snapshot();
    if (m_state != State::Idle || m_facade.state() != BackendState::Ready ||
        s.phase != SessionPhase::Authenticated || !validTogetherUser(s.accountId) ||
        !invite.room.isValid() || !validTogetherUser(invite.inviterId) ||
        invite.inviterId == s.accountId)
        return false;
    if (!supportedLocalPlayback())
        return false;
    begin();
    m_joining = true;
    m_room = invite.room;
    m_inviter = invite.inviterId;
    log::info(lcTogether, "together.start", {"join", true});
    emit changed();
    const auto sent = send(TogetherCheck{m_room}, [this](const TogetherResult& r) {
        if (r.outcome != TogetherOutcome::Acknowledged) {
            end(r.outcome == TogetherOutcome::Rejected ? Reason::Rejected : Reason::Failed, false);
            return;
        }
        if (r.joinable != true) {
            end(Reason::NotJoinable, false);
            return;
        }
        const auto sentJoin =
            send(TogetherJoin{m_room, m_inviter}, [this](const TogetherResult& r) { joined(r); });
        if (!sentJoin)
            end(Reason::Failed, false);
    });
    if (!sent)
        end(Reason::Failed, false);
    return m_state == State::Starting;
}

void TogetherSession::joined(const TogetherResult& r)
{
    if (r.outcome != TogetherOutcome::Acknowledged || !r.room) {
        end(r.outcome == TogetherOutcome::Rejected ? Reason::Rejected : Reason::Failed, false);
        return;
    }
    m_creator = r.room->creatorId.value_or(m_inviter);
    m_members = r.room->members;
    readRoom();
}

bool TogetherSession::resume(const Saved& saved)
{
    const auto s = m_facade.snapshot();
    if (m_state != State::Idle || m_facade.state() != BackendState::Ready ||
        s.phase != SessionPhase::Authenticated || !validTogetherUser(s.accountId) ||
        saved.accountId != s.accountId || !saved.room.isValid())
        return false;
    if (!supportedLocalPlayback())
        return false;
    begin();
    m_joining = saved.joining;
    m_resuming = true;
    m_room = saved.room;
    log::info(lcTogether, "together.start", {{"join", m_joining}, {"resume", true}});
    emit changed();
    // As the app does after a restart: still in this room, then carry on in it.
    const auto sent = send(TogetherStatus{}, [this](const TogetherResult& r) {
        if (r.outcome != TogetherOutcome::Acknowledged) {
            end(r.outcome == TogetherOutcome::Rejected ? Reason::Rejected : Reason::Failed, false);
            return;
        }
        if (r.inRoom != true || !r.room || r.room->id != m_room) {
            end(Reason::RoomEnded, false);
            return;
        }
        m_creator = r.room->creatorId.value_or(m_joining ? QString() : m_me);
        m_members = r.room->members;
        readRoom();
    });
    if (!sent)
        end(Reason::Failed, false);
    return m_state == State::Starting;
}

void TogetherSession::readRoom()
{
    const auto sent = send(TogetherQueue{m_room, localList()}, [this](const TogetherResult& r) {
        if (r.outcome != TogetherOutcome::Acknowledged || !r.playlist) {
            end(roomOver(r) ? Reason::RoomEnded : Reason::Failed, !roomOver(r));
            return;
        }
        const auto& p = *r.playlist;
        if (effective(p.display, p.random, p.mode.value_or(TogetherPlayMode::OrderLoop))
                .isEmpty()) {
            end(Reason::EmptyRoom, true);
            return;
        }
        applyList(p, r.reportedPlay ? &*r.reportedPlay : nullptr);
        if (r.reportedPlay)
            applyPlay(*r.reportedPlay, true);
        // Tells the service a member who accepted has the room's list.
        if (!m_resuming)
            send(TogetherSyncNotice{m_room}, [](const TogetherResult&) {});
        activate();
    });
    if (!sent)
        end(Reason::Failed, true);
}

void TogetherSession::activate()
{
    if (m_state != State::Starting)
        return;
    m_state = State::Active;
    m_lastOk = now();
    log::info(lcTogether, "together.active",
              {{"join", m_joining},
               {"resume", m_resuming},
               {"members", m_members.size()},
               {"songs", roomOrder().size()}});
    m_pollTimer.start(m_timing.pollMs);
    m_statusTimer.start(m_timing.statusMs);
    m_beatTimer.start(m_timing.heartbeatMs);
    emit changed();
    QTimer::singleShot(0, this, [this, generation = m_generation] {
        if (m_generation == generation && m_state == State::Active)
            heartbeat();
    });
}

// ---- the loop ----

bool TogetherSession::roomOver(const TogetherResult& r) const
{
    return (r.outcome == TogetherOutcome::Rejected && r.providerCode == kRoomEndedCode) ||
           r.resultType == u"ERROR_ROOM_INVALID";
}

void TogetherSession::readFailed(const TogetherResult& r)
{
    if (roomOver(r)) {
        end(Reason::RoomEnded, false);
        return;
    }
    if (now() - m_lastOk >= m_timing.lostMs) {
        log::warn(lcTogether, "together.connection_lost", {"error", r.error});
        end(Reason::ConnectionLost, true);
        return;
    }
    if (now() - m_lastOk >= 2 * m_timing.pollMs)
        setConnected(false);
}

void TogetherSession::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    log::info(lcTogether, "together.connected", {"connected", connected});
    emit changed();
}

void TogetherSession::poll()
{
    if (m_state != State::Active || m_polling)
        return;
    m_polling = send(TogetherQueue{m_room, ownList()}, [this](const TogetherResult& r) {
        m_polling = 0;
        if (m_state != State::Active)
            return;
        if (r.outcome != TogetherOutcome::Acknowledged || !r.playlist) {
            readFailed(r);
            return;
        }
        m_lastOk = now();
        setConnected(true);
        applyList(*r.playlist, r.reportedPlay ? &*r.reportedPlay : nullptr);
        if (m_state == State::Active && r.reportedPlay)
            applyPlay(*r.reportedPlay, false);
    });
    if (!m_polling)
        readFailed({});
}

void TogetherSession::status()
{
    if (m_state != State::Active || m_statusing)
        return;
    m_statusing = send(TogetherStatus{}, [this](const TogetherResult& r) {
        m_statusing = 0;
        if (m_state != State::Active || r.outcome != TogetherOutcome::Acknowledged)
            return;
        if (r.inRoom == false || (r.room && r.room->id != m_room)) {
            end(Reason::RoomEnded, false);
            return;
        }
        if (r.room &&
            (r.room->members != m_members || r.room->creatorId.value_or(m_creator) != m_creator)) {
            m_members = r.room->members;
            m_creator = r.room->creatorId.value_or(m_creator);
            log::info(lcTogether, "together.members", {"count", m_members.size()});
            emit changed();
        }
    });
}

void TogetherSession::heartbeat()
{
    if (m_state != State::Active || m_beating || !supportedLocalPlayback())
        return;
    const auto order = roomOrder();
    SongId song = order.contains(m_local.song) ? m_local.song : SongId{};
    if (song.value.isEmpty() && !order.isEmpty())
        song = order.first();
    if (!validMusicId(song.value))
        return;
    const auto progress =
        song == m_local.song ? std::clamp<qint64>(m_local.positionMs, 0, kProgressMax) : 0;
    m_beatSentAt = now();
    m_beating = send(
        TogetherHeartbeat{m_room, song,
                          playing(m_local) ? TogetherPlayStatus::Play : TogetherPlayStatus::Pause,
                          progress, m_versions},
        [this](const TogetherResult& r) {
            m_beating = 0;
            if (m_state != State::Active)
                return;
            if (roomOver(r)) {
                end(Reason::RoomEnded, false);
                return;
            }
            if (r.serverTimeMs) {
                const auto at = now();
                m_offset = *r.serverTimeMs - (m_beatSentAt + (at - m_beatSentAt) / 2);
            }
        });
}

void TogetherSession::stopAll()
{
    for (auto* t : {&m_pollTimer, &m_statusTimer, &m_beatTimer, &m_reportTimer, &m_replaceTimer})
        t->stop();
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        if (it.key() != m_leaving)
            m_facade.cancelTogether(it.key());
    const auto leaving = m_pending.take(m_leaving);
    m_pending.clear();
    if (m_leaving && leaving)
        m_pending.insert(m_leaving, leaving);
    m_polling = m_statusing = m_beating = m_replacing = 0;
    m_expect.reset();
    m_seek.reset();
    m_out.reset();
    m_afterReplace.reset();
}

void TogetherSession::end(Reason reason, bool sendEnd)
{
    if (m_state == State::Idle)
        return;
    const auto room = m_room;
    stopAll();
    if (reason == Reason::Left && m_state != State::Leaving && room.isValid()) {
        m_state = State::Leaving;
        m_leaving = send(TogetherEnd{room}, [this](const TogetherResult&) {
            m_leaving = 0;
            m_leaveTimer.stop();
            end(Reason::Left, false);
        });
        if (m_leaving) {
            m_leaveTimer.start(m_timing.leaveMs);
            emit changed();
            return;
        }
    } else if (sendEnd && room.isValid() && reason != Reason::Left) {
        send(TogetherEnd{room}, [](const TogetherResult&) {}); // best effort
    }
    m_leaveTimer.stop();
    m_leaving = 0;
    m_state = State::Idle;
    m_player.setBiliSourcePermitted(true);
    log::info(lcTogether, "together.end", {"reason", reason});
    // Results still in flight (the best-effort end) belong to the session that ended.
    ++m_generation;
    m_room = {};
    m_members.clear();
    m_creator.clear();
    m_connected = true;
    emit changed();
    emit ended(reason);
}

void TogetherSession::leave()
{
    if (m_state == State::Starting || m_state == State::Active)
        end(Reason::Left, true);
}

void TogetherSession::close()
{
    // The official app keeps the room when it goes away; resume() picks it up again.
    if (m_state != State::Idle)
        log::info(lcTogether, "together.close", {"state", m_state});
    stopAll();
    m_player.setBiliSourcePermitted(true);
    m_leaveTimer.stop();
    ++m_generation;
    disconnect(&m_facade, nullptr, this, nullptr);
    disconnect(&m_player, nullptr, this, nullptr);
}

std::optional<TogetherSession::Saved> TogetherSession::saved() const
{
    if (m_state != State::Active || !m_room.isValid() || m_me.isEmpty())
        return std::nullopt;
    return Saved{m_room, m_me, m_joining};
}

QString TogetherSession::shareLink() const
{
    if (m_state != State::Active || !m_room.isValid() || m_me.isEmpty())
        return {};
    QUrl url(kShareLink);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("roomId"), m_room.value);
    q.addQueryItem(QStringLiteral("inviterId"), m_me);
    if (const auto order = roomOrder(); order.contains(m_local.song))
        q.addQueryItem(QStringLiteral("songId"), m_local.song.value);
    url.setQuery(q);
    return url.toString(QUrl::FullyEncoded);
}

// ---- the room's state applied locally ----

qint64 TogetherSession::sinceServer(qint64 serverSeq, bool first) const
{
    // serverSeq is the service's clock when it took the command (2026-11-22 observed). The
    // command a room is joined or resumed at may be old, and comes before the first heartbeat
    // tells the offset: the local clock stands in then (a position past the song's end is
    // not sought).
    if (serverSeq <= 0 || (!m_offset && !first))
        return 0;
    return std::clamp<qint64>(now() + m_offset.value_or(0) - serverSeq, 0,
                              first ? kProgressMax : 10000);
}

QList<SongId> TogetherSession::roomOrder() const { return effective(m_display, m_random, m_mode); }
bool TogetherSession::inStep() const
{
    return localOrder() == roomOrder().mid(0, kTogetherSongsMax);
}

void TogetherSession::applyList(const TogetherPlaylist& p, const TogetherReportedPlay* play)
{
    // A REPLACE this client sent and the service has not taken yet: the read is older.
    for (const auto& v : p.versions)
        for (const auto& mine : std::as_const(m_versions))
            if (v.userId == m_me && mine.userId == m_me && v.version < mine.version)
                return;
    if (m_replacing)
        return;
    if (!p.versions.isEmpty())
        m_versions = p.versions;
    // An unchanged list may come back empty: keep the last one then.
    if (p.displayChanged || !p.display.isEmpty())
        m_display = p.display;
    if (p.randomChanged || !p.random.isEmpty())
        m_random = p.random;
    if (p.mode)
        m_mode = *p.mode;
    // The local queue holds the first kTogetherSongsMax songs of a longer list.
    const auto order = roomOrder().mid(0, kTogetherSongsMax);
    if (!order.isEmpty() && (order != localOrder() || m_forceRoomReload)) {
        m_forceRoomReload = false;
        rebuild(order, m_mode, play);
    }
}

void TogetherSession::rebuild(const QList<SongId>& order, TogetherPlayMode mode,
                              const TogetherReportedPlay* play)
{
    qsizetype start = 0;
    if (play && order.contains(play->target))
        start = order.indexOf(play->target);
    else if (order.contains(m_local.song))
        start = order.indexOf(m_local.song);
    // replaceQueue plays; the command (applied next) or the earlier state decides. The
    // player reports what it does at once, so the expectation comes first.
    const bool on = play ? play->status == TogetherPlayStatus::Play : playing(m_local);
    m_expect = Expect{.song = order[start], .playing = on, .until = now() + m_timing.settleMs};
    if (!play || play->target != order[start])
        m_seek.reset();
    log::info(lcTogether, "together.list", {{"songs", order.size()}, {"mode", int(mode)}});
    m_rebuilding = true;
    const QPointer<TogetherSession> self(this);
    const bool ok = m_player.replaceQueue(order, start, m_quality());
    if (!self)
        return;
    if (ok)
        m_player.setModes(QueueOrder::Sequential, mode == TogetherPlayMode::SingleLoop
                                                      ? QueueRepeat::One
                                                      : QueueRepeat::All);
    m_rebuilding = false;
    if (!ok) {
        log::warn(lcTogether, "together.list_not_applied", {"songs", order.size()});
        return;
    }
    if (!on)
        m_player.pause();
}

void TogetherSession::applyPlay(const TogetherReportedPlay& c, bool force)
{
    if (!force && c.serverSeq <= m_serverSeq)
        return;
    m_serverSeq = std::max(m_serverSeq, c.serverSeq);
    // Forced (joining or resuming) the last command is the room's state, whoever sent it.
    if (!force && (c.userId == m_me || c.serverSeq <= 0))
        return;
    const auto order = localOrder();
    const auto target = order.contains(c.target) ? c.target : m_local.song;
    if (!validMusicId(target.value))
        return;
    const bool on = c.status == TogetherPlayStatus::Play;
    const auto position = std::clamp<qint64>(
        c.progressMs + (on ? sinceServer(c.serverSeq, force) : 0), 0, kProgressMax);
    log::debug(lcTogether, "together.apply",
               {{"action", c.action}, {"play", on}, {"song_changes", target != m_local.song}});
    m_expect = Expect{.song = target, .playing = on, .until = now() + m_timing.settleMs};
    if (target != m_local.song) {
        const auto q = m_player.queue();
        const auto playOrder = m_player.playOrder();
        for (const auto& id : playOrder) {
            const auto e = std::find_if(q.entries.cbegin(), q.entries.cend(),
                                        [&](const QueueEntry& x) { return x.id == id; });
            if (e != q.entries.cend() && e->song == target) {
                if (e->id != q.active)
                    m_player.select(e->id);
                break;
            }
        }
        m_seek = PendingSeek{target, position, now(), on};
    } else {
        const auto drift = std::abs(m_local.positionMs - position);
        if (c.action == TogetherPlayAction::Progress || drift > m_timing.correctMs)
            m_seek = PendingSeek{target, position, now(), on};
    }
    if (on && !playing(m_local))
        m_player.resume();
    else if (!on && playing(m_local))
        m_player.pause();
    seekPending();
}

void TogetherSession::seekPending()
{
    if (!m_seek || m_local.song != m_seek->song || !m_local.seekable ||
        (m_local.state != PlaybackState::Playing && m_local.state != PlaybackState::Paused &&
         m_local.state != PlaybackState::Ready))
        return;
    auto target = m_seek->positionMs + (m_seek->playing ? now() - m_seek->at : 0);
    m_seek.reset();
    if (m_local.durationMs && target >= *m_local.durationMs)
        return;
    if (std::abs(m_local.positionMs - target) <= 300)
        return;
    if (!m_expect)
        m_expect = Expect{.song = m_local.song, .playing = playing(m_local)};
    m_expect->until = now() + m_timing.settleMs;
    m_expect->seeking = m_player.seek(std::max<qint64>(target, 0));
}

// ---- local changes reported ----

bool TogetherSession::expecting() const { return m_expect && now() < m_expect->until; }
void TogetherSession::settle(const PlaybackSnapshot& s)
{
    // Done once the player shows the song in the state asked for, with no seek to come.
    if (m_expect && s.song == m_expect->song && playing(s) == m_expect->playing &&
        !m_expect->seeking && !m_seek &&
        (s.state == PlaybackState::Playing || s.state == PlaybackState::Paused))
        m_expect.reset();
}

void TogetherSession::playbackChanged(const PlaybackSnapshot& s)
{
    const auto old = m_local;
    m_local = s;
    if (m_state != State::Idle && !supportedLocalPlayback()) {
        end(Reason::UnsupportedSource, true);
        return;
    }
    if (m_state != State::Active)
        return;
    seekPending();
    if (!validMusicId(s.song.value))
        return;
    if (!expecting())
        m_expect.reset();
    std::optional<TogetherPlayAction> action;
    auto trigger = TogetherTrigger::Manual;
    if (s.song != old.song) {
        // Ended, or played on into the preloaded next song (gapless) near its end.
        const auto order = localOrder();
        const auto at = order.indexOf(old.song);
        const bool ended = old.state == PlaybackState::Ended ||
                           (at >= 0 && order.value((at + 1) % order.size()) == s.song &&
                            old.durationMs && old.positionMs >= *old.durationMs - 3000);
        action = ended ? TogetherPlayAction::Next : TogetherPlayAction::Goto;
        trigger = ended ? TogetherTrigger::Auto : TogetherTrigger::Manual;
    } else if (playing(s) != playing(old))
        action = playing(s) ? TogetherPlayAction::Play : TogetherPlayAction::Pause;
    else if (s.token == old.token && s.seekRevision > old.seekRevision)
        action = TogetherPlayAction::Progress;
    if (!action) {
        if (m_expect)
            settle(s);
        return;
    }
    if (expecting()) {
        // What this session asked the player to do: the song, play/pause or the position.
        const bool same = s.song == m_expect->song;
        bool absorbed = false;
        if (*action == TogetherPlayAction::Goto || *action == TogetherPlayAction::Next)
            absorbed = same;
        else if (*action == TogetherPlayAction::Progress)
            absorbed = same && std::exchange(m_expect->seeking, false);
        else
            absorbed = same && playing(s) == m_expect->playing;
        if (absorbed) {
            settle(s);
            return;
        }
    }
    m_expect.reset();
    // A song of a new local list is sent after that list (sendLocalPlay).
    if (!roomOrder().contains(s.song) && !localOrder().contains(s.song))
        return;
    const auto former = m_out && (m_out->action == TogetherPlayAction::Goto ||
                                  m_out->action == TogetherPlayAction::Next)
                            ? m_out->former
                            : (validMusicId(old.song.value) ? old.song : s.song);
    m_out = TogetherPlay{m_room,
                         *action,
                         former,
                         s.song,
                         playing(s) ? TogetherPlayStatus::Play : TogetherPlayStatus::Pause,
                         std::clamp<qint64>(s.song == old.song ? s.positionMs : 0, 0, kProgressMax),
                         0,
                         trigger};
    m_reportTimer.start(m_timing.reportMs);
}

void TogetherSession::sendLocalPlay()
{
    if (!m_out || m_state != State::Active)
        return;
    auto p = *std::exchange(m_out, std::nullopt);
    // A song change carries the position now; a seek or a pause theirs.
    if (m_local.song == p.target && p.action != TogetherPlayAction::Goto &&
        p.action != TogetherPlayAction::Next)
        p.progressMs = std::clamp<qint64>(m_local.positionMs, 0, kProgressMax);
    if (m_replacing || m_replaceTimer.isActive() || !inStep()) {
        m_afterReplace = p; // the list first, so the song is in it
        if (!m_replacing && !m_replaceTimer.isActive())
            m_replaceTimer.start(m_timing.replaceMs);
        return;
    }
    if (roomOrder().contains(p.target))
        sendPlay(p);
}

void TogetherSession::sendPlay(TogetherPlay p)
{
    if (!supportedLocalPlayback())
        return;
    p.clientSeq = m_seq++;
    log::debug(lcTogether, "together.report", {{"action", p.action}, {"seq", p.clientSeq}});
    send(p, [](const TogetherResult&) {});
}

void TogetherSession::queueChanged()
{
    if (m_state != State::Idle && !supportedLocalPlayback()) {
        end(Reason::UnsupportedSource, true);
        return;
    }
    if (m_state != State::Active || m_rebuilding)
        return;
    const auto order = localOrder();
    if (order.isEmpty() || inStep())
        return;
    m_replaceTimer.start(m_timing.replaceMs);
}

void TogetherSession::sendReplace()
{
    if (m_state != State::Active || m_replacing)
        return;
    const auto list = localList();
    if (list.display.isEmpty() || inStep()) {
        if (m_afterReplace && inStep() && roomOrder().contains(m_afterReplace->target))
            sendPlay(*std::exchange(m_afterReplace, std::nullopt));
        return;
    }
    auto versions = m_versions;
    const auto mine = std::find_if(versions.begin(), versions.end(),
                                   [&](const TogetherVersion& v) { return v.userId == m_me; });
    if (mine != versions.end())
        ++mine->version;
    else
        versions.append({m_me, 0});
    m_display = list.display;
    m_random = list.random;
    m_mode = list.mode;
    m_versions = versions;
    log::info(lcTogether, "together.replace", {"songs", list.display.size()});
    m_replacing = send(TogetherReplaceQueue{m_room, list, versions, m_seq++},
                       [this](const TogetherResult& r) {
                           m_replacing = 0;
                           if (m_state != State::Active)
                               return;
                           if (r.outcome != TogetherOutcome::Acknowledged)
                               log::warn(lcTogether, "together.replace_failed",
                                         {{"outcome", r.outcome}, {"error", r.error}});
                           else
                               emit listSent();
                           if (m_afterReplace)
                               sendPlay(*std::exchange(m_afterReplace, std::nullopt));
                       });
}

// ---- the local queue described ----

QList<SongId> TogetherSession::localOrder() const
{
    const auto q = m_player.queue();
    QHash<QUuid, SongId> byEntry;
    for (const auto& e : q.entries)
        byEntry.insert(e.id, e.song);
    QList<SongId> out;
    for (const auto& id : m_player.playOrder())
        if (const auto song = byEntry.value(id); validMusicId(song.value))
            out.append(song);
    return out.mid(0, kTogetherSongsMax);
}

TogetherList TogetherSession::localList() const
{
    const auto q = m_player.queue();
    TogetherList list;
    QHash<QUuid, SongId> byEntry;
    for (const auto& e : q.entries) {
        if (list.display.size() == kTogetherSongsMax || !validMusicId(e.song.value))
            continue;
        if (e.id == q.active) {
            list.anchor = e.song;
            list.anchorPosition = list.display.size();
        }
        byEntry.insert(e.id, e.song);
        list.display.append(e.song);
    }
    if (list.anchor.value.isEmpty() && !list.display.isEmpty()) {
        list.anchor = list.display.first();
        list.anchorPosition = 0;
    }
    if (q.repeat == QueueRepeat::One)
        list.mode = TogetherPlayMode::SingleLoop;
    else if (q.order == QueueOrder::Shuffle) {
        list.mode = TogetherPlayMode::Random;
        for (const auto& id : m_player.playOrder())
            if (byEntry.contains(id))
                list.random.append(byEntry.value(id));
    }
    if (!validTogetherList(list)) {
        list.random.clear();
        if (list.mode == TogetherPlayMode::Random)
            list.mode = TogetherPlayMode::OrderLoop;
    }
    return list;
}

TogetherList TogetherSession::ownList() const
{
    // In step with the room: its lists, at the song playing here.
    if (inStep() && !m_display.isEmpty()) {
        TogetherList list;
        list.display = m_display.mid(0, kTogetherSongsMax);
        list.random = m_random.size() == list.display.size() ? m_random : QList<SongId>{};
        list.mode = m_mode;
        const auto i = list.display.indexOf(m_local.song);
        if (i >= 0) {
            list.anchor = m_local.song;
            list.anchorPosition = i;
        }
        if (!validTogetherList(list))
            list.random.clear();
        if (validTogetherList(list))
            return list;
    }
    return localList();
}
} // namespace cirrus::application
