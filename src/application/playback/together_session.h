#pragma once
#include "application/backend/backend_facade.h"
#include "domain/music/together_types.h"
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <optional>

namespace cirrus::application
{
class ListeningSession;

// Listen together over HTTP polling, as the official Android app 9.6.22 does it but without
// NIM (docs/api/together-polling.md §3, §5). One room at a time:
//  - create(): a room from the local queue (status, create, REPLACE with the queue, the
//    song playing now as the first command); join(): check, accept, read the room's list,
//    play it locally from the room's last command, sync/notice.
//  - While active it reads the room (sync/playlist/get) every 2 s, the members (status)
//    every 10 s and sends a heartbeat every 30 s. Another member's newer command (by
//    serverSeq; each member counts clientSeq on its own) is applied to the local player:
//    its song, play/pause and position. The room's list replaces the local queue when it
//    differs.
//  - Local changes are reported: a song change (GOTO, or NEXT AUTO after a song ended),
//    play/pause and seeks, only for songs in the room's list; a changed local queue is sent
//    as REPLACE. What the session itself did to apply a command is not reported back.
//  - It ends on leave(), when the service says the room is over (code 488, room invalid,
//    or no longer in a room), on a sign-out, or after a minute without a successful read.
//  - Closing the app does not leave the room (close()); saved() is what resume() needs to
//    pick it up on the next start: status, and while still in that room, its list and last
//    command, like the official app (no accept again).
// Caller-thread object; facade and player must outlive it.
class TogetherSession : public QObject
{
    Q_OBJECT
  public:
    enum class State { Idle, Starting, Active, Leaving };
    // Why the session is back to Idle (ended()). The service's hintText, when it sent one,
    // is hint() (for display, never logged).
    enum class Reason {
        Left,           // leave()
        RoomEnded,      // the room is over (another member ended it)
        SignedOut,      // the account changed
        ConnectionLost, // no successful read for Timing::lostMs
        NotJoinable,    // room/check said no
        EmptyRoom,      // the room has no songs to play
        Rejected,       // the service refused create/join
        Failed,         // no answer or an answer this client cannot read
        UnsupportedSource
    };
    struct Timing {
        int pollMs = 2000;
        int statusMs = 10000;
        int heartbeatMs = 30000; // the heartbeat answers timeSpan 30 (s)
        int lostMs = 60000;
        int settleMs = 5000;  // how long the player may take to follow an applied command
        int reportMs = 200;   // local changes wait this long, so the last of a burst is sent
        int replaceMs = 300;  // local queue edits likewise
        int leaveMs = 3000;   // leave() waits this long for end/v2
        int correctMs = 1500; // a position this far from the room's is corrected
    };
    struct Invite {
        domain::TogetherRoomId room;
        QString inviterId;
    };
    // A share link (…/listen-together/share/index.html?roomId=…&inviterId=…), alone or in
    // the text the official app copies around it.
    static std::optional<Invite> parseInvite(const QString& text);
    // An active room, as resume() takes it after a restart.
    struct Saved {
        domain::TogetherRoomId room;
        QString accountId;
        bool joining = false; // joined another member's room (else this account's own)
    };

    TogetherSession(BackendFacade& facade, ListeningSession& player, QObject* parent = nullptr);
    ~TogetherSession() override;

    void setQuality(std::function<domain::AudioQuality()> quality)
    {
        m_quality = std::move(quality);
    }
    void setTiming(const Timing& timing) { m_timing = timing; }
    // The local wall clock in ms since the epoch (tests); serverSeq is the service's.
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }

    // False when it cannot start now: not idle, not signed in, or (create) no local queue.
    [[nodiscard]] bool canCreate() const;
    bool create();
    bool join(const Invite& invite);
    // Back into a saved room, when the account is the same and the service still has it in
    // that room; ends RoomEnded otherwise.
    bool resume(const Saved& saved);
    void leave();
    // Closing the app: stops without leaving the room and without signals; nothing after.
    void close();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] domain::TogetherRoomId room() const { return m_room; }
    [[nodiscard]] bool isCreator() const { return !m_me.isEmpty() && m_creator == m_me; }
    [[nodiscard]] QString accountId() const { return m_me; }
    [[nodiscard]] QString creatorId() const { return m_creator; }
    [[nodiscard]] bool joining() const { return m_joining; }
    [[nodiscard]] bool resuming() const { return m_resuming; } // the last start was resume()
    // Active: the room to resume after a restart.
    [[nodiscard]] std::optional<Saved> saved() const;
    [[nodiscard]] QList<domain::TogetherMember> members() const { return m_members; }
    // The link another member opens to join; empty unless active.
    [[nodiscard]] QString shareLink() const;
    // False while reads fail (Active only); the session ends after Timing::lostMs.
    [[nodiscard]] bool connected() const { return m_connected; }
    [[nodiscard]] QString hint() const { return m_hint; }

  signals:
    void changed(); // state, members, connection or hint: read the getters
    void ended(cirrus::application::TogetherSession::Reason reason);
    void listSent(); // the service took this client's changed queue as the room's list

  private:
    using Handler = std::function<void(const domain::TogetherResult&)>;
    struct Expect { // what the player is to do because of an applied command
        domain::SongId song;
        bool playing = false;
        bool seeking = false; // a seek it issued has not shown yet
        qint64 until = 0;
    };
    struct PendingSeek {
        domain::SongId song;
        qint64 positionMs = 0, at = 0;
        bool playing = false;
    };

    quint64 send(domain::TogetherAction action, Handler then);
    void completed(quint64 id, domain::SessionToken context, const domain::TogetherResult& r);
    void sessionChanged(const domain::SessionSnapshot& snapshot);
    void begin();
    void createRoom();
    void replaceInitial();
    void joined(const domain::TogetherResult& r);
    void readRoom();
    void activate();
    void poll();
    void status();
    void heartbeat();
    void readFailed(const domain::TogetherResult& r);
    bool roomOver(const domain::TogetherResult& r) const;
    void end(Reason reason, bool sendEnd);
    void stopAll();
    [[nodiscard]] bool supportedLocalPlayback() const;
    bool m_forceRoomReload = false;

    void applyList(const domain::TogetherPlaylist& p, const domain::TogetherReportedPlay* play);
    void applyPlay(const domain::TogetherReportedPlay& c, bool force);
    void rebuild(const QList<domain::SongId>& order, domain::TogetherPlayMode mode,
                 const domain::TogetherReportedPlay* play);

    void playbackChanged(const domain::PlaybackSnapshot& s);
    void queueChanged();
    void sendLocalPlay();
    void sendReplace();
    void sendPlay(domain::TogetherPlay p);
    void seekPending();

    [[nodiscard]] QList<domain::SongId> localOrder() const;
    [[nodiscard]] domain::TogetherList localList() const;
    [[nodiscard]] domain::TogetherList ownList() const; // the Queue request's playlistParam
    [[nodiscard]] QList<domain::SongId> roomOrder() const;
    [[nodiscard]] bool inStep() const; // the local queue plays the room's list
    [[nodiscard]] bool expecting() const;
    void settle(const domain::PlaybackSnapshot& s);
    [[nodiscard]] qint64 now() const { return m_clock(); }
    [[nodiscard]] qint64 sinceServer(qint64 serverSeq, bool first) const;
    void setConnected(bool connected);

    BackendFacade& m_facade;
    ListeningSession& m_player;
    Timing m_timing;
    std::function<domain::AudioQuality()> m_quality;
    std::function<qint64()> m_clock;

    State m_state = State::Idle;
    bool m_joining = false, m_resuming = false;
    quint64 m_generation = 0;
    domain::SessionToken m_context;
    QString m_me, m_creator, m_inviter, m_hint;
    domain::TogetherRoomId m_room;
    QList<domain::TogetherMember> m_members;
    bool m_connected = true;
    qint64 m_lastOk = 0;

    // The room as last read (or as last sent by this client).
    QList<domain::SongId> m_display, m_random;
    domain::TogetherPlayMode m_mode = domain::TogetherPlayMode::OrderLoop;
    QList<domain::TogetherVersion> m_versions;
    qint64 m_serverSeq = -1;        // the newest command seen
    qint64 m_seq = 0;               // this client's clientSeq
    std::optional<qint64> m_offset; // service clock minus local clock

    QHash<quint64, Handler> m_pending;
    quint64 m_polling = 0, m_statusing = 0, m_beating = 0, m_replacing = 0, m_leaving = 0;
    qint64 m_beatSentAt = 0;
    bool m_rebuilding = false;
    std::optional<Expect> m_expect;
    std::optional<PendingSeek> m_seek;
    domain::PlaybackSnapshot m_local;
    std::optional<domain::TogetherPlay> m_out, m_afterReplace;
    QTimer m_pollTimer, m_statusTimer, m_beatTimer, m_reportTimer, m_replaceTimer, m_leaveTimer;
};
CIRRUS_LOG_ENUM(TogetherSession::State, Idle, Starting, Active, Leaving)
CIRRUS_LOG_ENUM(TogetherSession::Reason, Left, RoomEnded, SignedOut, ConnectionLost, NotJoinable,
                EmptyRoom, Rejected, Failed, UnsupportedSource)
} // namespace cirrus::application
Q_DECLARE_METATYPE(cirrus::application::TogetherSession::Reason)
