#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

// Listen together as the dialog and the players see it (pages/together.md).
namespace cirrus::ui::together
{
Q_NAMESPACE
QML_NAMED_ELEMENT(TogetherState)
enum State { Idle, Starting, Active, Leaving };
Q_ENUM_NS(State)
// What an invite link is to this account.
enum Link { NoLink, Invite, OwnRoom };
Q_ENUM_NS(Link)
// Why a session ended (TogetherSession::Reason, the same order).
enum End { Left, RoomEnded, SignedOut, ConnectionLost, NotJoinable, EmptyRoom, Rejected, Failed,
           UnsupportedSource };
} // namespace cirrus::ui::together

namespace cirrus::ui
{

struct TogetherMemberInfo {
    QString key, name, avatarKey;
    bool host = false, self = false;
    friend bool operator==(const TogetherMemberInfo&, const TogetherMemberInfo&) = default;
};
struct TogetherSnapshot {
    together::State state = together::Idle;
    bool joining = false;   // Starting: joining a room (else creating one)
    bool resuming = false;  // Starting: back into the room of the last run
    bool canCreate = false; // signed in, idle and a queue to start from
    int queueCount = 0;     // songs the room would start with
    QList<TogetherMemberInfo> members;
    bool connected = true;
    QString shareLink; // empty unless this account's own room is active
    QString hint;      // the service's text for the last refusal
    friend bool operator==(const TogetherSnapshot&, const TogetherSnapshot&) = default;
};

// Listen together (pages/together.md). The app has one (BackendTogether); the gallery none.
class TogetherSource
{
  public:
    virtual ~TogetherSource() = default;
    [[nodiscard]] virtual const TogetherSnapshot& snapshot() const = 0;
    virtual void setHandlers(std::function<void()> changed,
                             std::function<void(together::End)> ended,
                             std::function<void()> listSent) = 0;
    virtual bool create() = 0;
    virtual bool join(const QString& link) = 0;
    virtual void leave() = 0;
    [[nodiscard]] virtual together::Link linkKind(const QString& link) const = 0;
};

// App.together: the session's state for the dialog and the marks on the players, and its
// notices. Without a source (the gallery) it stays idle and unavailable.
class Together : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.together")
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(int state READ state NOTIFY changed) // TogetherState.State
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool joining READ joining NOTIFY changed)
    Q_PROPERTY(bool resuming READ resuming NOTIFY changed)
    Q_PROPERTY(bool canCreate READ canCreate NOTIFY changed)
    Q_PROPERTY(int queueCount READ queueCount NOTIFY changed)
    // [{key, name, avatarKey, host, self}], the service's order.
    Q_PROPERTY(QVariantList members READ members NOTIFY changed)
    Q_PROPERTY(QString partnerName READ partnerName NOTIFY changed) // the other member, or ""
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString shareLink READ shareLink NOTIFY changed) // only while waiting for a friend

  public:
    explicit Together(TogetherSource* source, QObject* parent = nullptr);

    [[nodiscard]] bool available() const { return m_source; }
    [[nodiscard]] int state() const { return m_snapshot.state; }
    [[nodiscard]] bool active() const { return m_snapshot.state == together::Active; }
    [[nodiscard]] bool joining() const { return m_snapshot.joining; }
    [[nodiscard]] bool resuming() const { return m_snapshot.resuming; }
    [[nodiscard]] bool canCreate() const { return m_snapshot.canCreate; }
    [[nodiscard]] int queueCount() const { return m_snapshot.queueCount; }
    [[nodiscard]] QVariantList members() const;
    [[nodiscard]] QString partnerName() const;
    [[nodiscard]] bool connected() const { return m_snapshot.connected; }
    [[nodiscard]] QString shareLink() const;

    Q_INVOKABLE void create();
    Q_INVOKABLE void join(const QString& link);
    Q_INVOKABLE void leave();
    Q_INVOKABLE void copyLink();
    // TogetherState.Link for the text in the join field.
    Q_INVOKABLE int linkKind(const QString& link) const;
    // The clipboard's text when it is an invite link, else "" (read only when asked).
    Q_INVOKABLE QString clipboardInvite() const;

  signals:
    void changed();
    void notice(const QString& message);

  private:
    void update();
    void ended(together::End reason);

    TogetherSource* m_source = nullptr;
    TogetherSnapshot m_snapshot;
    bool m_copyOnActive = false; // a room this account creates: copy its link once it is up
};

} // namespace cirrus::ui
