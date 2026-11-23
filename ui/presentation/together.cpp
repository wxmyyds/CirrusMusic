#include "together.h"
#include "notice.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSet>
#include <QVariantMap>

namespace cirrus::ui
{

Together::Together(TogetherSource* source, QObject* parent) : QObject(parent), m_source(source)
{
    if (!m_source)
        return;
    m_source->setHandlers(
        [this] { update(); }, [this](together::End reason) { ended(reason); },
        [this] { emit notice(noticed("together.list_sent", tr("房间里的歌换成了你的播放队列"))); });
    m_snapshot = m_source->snapshot();
}

QVariantList Together::members() const
{
    QVariantList out;
    for (const auto& m : m_snapshot.members)
        out.append(QVariantMap{{QStringLiteral("key"), m.key},
                               {QStringLiteral("name"), m.name},
                               {QStringLiteral("avatarKey"), m.avatarKey},
                               {QStringLiteral("host"), m.host},
                               {QStringLiteral("self"), m.self}});
    return out;
}

QString Together::partnerName() const
{
    for (const auto& m : m_snapshot.members)
        if (!m.self)
            return m.name.isEmpty() ? tr("好友") : m.name;
    return {};
}

QString Together::shareLink() const
{
    // A two-person room (pages/together.md LT6): only while this account's room waits.
    int others = 0;
    for (const auto& m : m_snapshot.members)
        others += m.self ? 0 : 1;
    return others ? QString() : m_snapshot.shareLink;
}

void Together::update()
{
    const auto before = m_snapshot;
    m_snapshot = m_source->snapshot();
    if (before.state == together::Active && m_snapshot.state == together::Active) {
        QSet<QString> had, has;
        for (const auto& m : before.members)
            if (!m.self)
                had.insert(m.key);
        for (const auto& m : m_snapshot.members)
            if (!m.self) {
                has.insert(m.key);
                if (!had.contains(m.key))
                    emit notice(
                        noticed("together.member_joined",
                                tr("%1 加入了一起听").arg(m.name.isEmpty() ? tr("好友") : m.name)));
            }
        for (const auto& m : before.members)
            if (!m.self && !has.contains(m.key))
                emit notice(noticed("together.member_left",
                                    tr("%1 离开了").arg(m.name.isEmpty() ? tr("好友") : m.name)));
    }
    if (before.state == together::Starting && m_snapshot.state == together::Active &&
        m_snapshot.resuming)
        emit notice(noticed("together.resumed", tr("已恢复一起听")));
    if (m_copyOnActive && m_snapshot.state == together::Active) {
        m_copyOnActive = false;
        if (!m_snapshot.shareLink.isEmpty()) {
            if (QClipboard* clipboard = QGuiApplication::clipboard())
                clipboard->setText(m_snapshot.shareLink);
            emit notice(noticed("together.created", tr("房间已创建，邀请链接已复制")));
        }
    }
    if (m_snapshot.state == together::Idle)
        m_copyOnActive = false;
    if (!(before == m_snapshot))
        emit changed();
}

void Together::ended(together::End reason)
{
    const bool joining = m_snapshot.joining;
    const bool resuming = m_snapshot.resuming;
    const auto hint = m_snapshot.hint;
    QString text;
    switch (reason) {
    case together::Left:
        text = noticed("together.left", tr("已离开一起听"));
        break;
    case together::RoomEnded:
        text = noticed("together.room_ended", tr("一起听已结束"));
        break;
    case together::SignedOut:
        text = noticed("together.signed_out", tr("已退出一起听"));
        break;
    case together::ConnectionLost:
        text = noticed("together.connection_lost", tr("连接断了，已退出一起听"));
        break;
    case together::NotJoinable:
        text = hint.isEmpty() ? noticed("together.not_joinable", tr("房间已结束或已满"))
                              : noticed("together.not_joinable_hint", hint);
        break;
    case together::EmptyRoom:
        text = noticed("together.empty_room", tr("房间里还没有歌"));
        break;
    case together::UnsupportedSource:
        text = noticed("together.unsupported_source", tr("一起听仅支持网易云歌曲，已退出"));
        break;
    case together::Rejected:
    case together::Failed:
        if (!hint.isEmpty())
            text = noticed("together.refused_hint", hint);
        else if (resuming)
            text = noticed("together.resume_failed", tr("没能恢复一起听"));
        else
            text = joining ? noticed("together.join_failed", tr("没能加入一起听"))
                           : noticed("together.create_failed", tr("没能创建房间"));
        break;
    }
    emit notice(text);
}

void Together::create()
{
    if (m_source && m_source->create())
        m_copyOnActive = true;
}

void Together::join(const QString& link)
{
    if (m_source)
        m_source->join(link);
}

void Together::leave()
{
    if (m_source)
        m_source->leave();
}

void Together::copyLink()
{
    const auto link = shareLink();
    if (link.isEmpty())
        return;
    if (QClipboard* clipboard = QGuiApplication::clipboard())
        clipboard->setText(link);
    emit notice(noticed("together.link_copied", tr("已复制邀请链接")));
}

int Together::linkKind(const QString& link) const
{
    return m_source ? m_source->linkKind(link) : together::NoLink;
}

QString Together::clipboardInvite() const
{
    const QClipboard* clipboard = QGuiApplication::clipboard();
    if (!m_source || !clipboard)
        return {};
    const auto text = clipboard->text().trimmed();
    return text.size() <= 2048 && m_source->linkKind(text) == together::Invite ? text : QString();
}

} // namespace cirrus::ui
