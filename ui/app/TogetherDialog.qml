pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Listen together (docs/ui/pages/together.md §2, §3): start a room from the queue or
// join a friend's by its invite link; in a room, who is in it, the connection and the
// link to send while waiting. 关闭 only closes the dialog; 离开 leaves the room.
Dialog {
    id: dialog

    readonly property var together: App.together
    readonly property int state: together.state
    readonly property bool idle: state === TogetherState.Idle
    readonly property bool busy: state === TogetherState.Starting || state === TogetherState.Leaving
    readonly property bool inRoom: state === TogetherState.Active
    readonly property int linkKind: together.linkKind(linkField.text)

    width: 480
    title: qsTr("一起听")
    rejectText: dialog.busy && dialog.state === TogetherState.Starting ? qsTr("取消") : qsTr("关闭")
    initialFocusItem: dialog.idle ? (linkField.text !== "" ? joinButton : linkField) : null
    onRejected: {
        if (dialog.state === TogetherState.Starting)
            dialog.together.leave();
    }
    onAboutToShow: {
        // LT3: an invite link on the clipboard fills the field, read only now.
        linkField.text = dialog.idle ? dialog.together.clipboardInvite() : "";
    }

    function join(): void {
        if (dialog.linkKind === TogetherState.Invite)
            dialog.together.join(linkField.text.trim());
    }

    buttons: [
        Button {
            visible: dialog.inRoom
            text: qsTr("离开")
            variant: Md.Text
            tint: Theme.color.error
            onClicked: dialog.together.leave()
        }
    ]

    // ---- not in a room ----
    Column {
        width: parent.width
        spacing: 20
        visible: dialog.idle

        Rectangle {
            width: parent.width
            height: createRow.implicitHeight + 32
            radius: 24
            color: Theme.color.surfaceContainerHighest

            Row {
                id: createRow
                x: 16
                y: 16
                width: parent.width - 32
                spacing: 16

                IconContainer {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Symbols.group_add
                    size: 48
                    tone: Md.Primary
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 48 - createButton.width - 32
                    spacing: 2

                    Text {
                        width: parent.width
                        text: qsTr("开一个房间")
                        elide: Text.ElideRight
                        color: Theme.color.onSurface
                        font: Theme.type.titleMedium.font
                    }
                    Text {
                        width: parent.width
                        text: dialog.together.queueCount > 0
                              ? qsTr("用现在的播放队列 · %1 首").arg(dialog.together.queueCount)
                              : qsTr("先播放一些歌")
                        elide: Text.ElideRight
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }
                }
                Button {
                    id: createButton
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("创建")
                    enabled: dialog.together.canCreate
                    onClicked: dialog.together.create()
                }
            }
        }

        Column {
            width: parent.width
            spacing: 8

            Text {
                text: qsTr("加入好友的房间")
                color: Theme.color.onSurface
                font: Theme.type.titleSmall.font
            }
            TextField {
                id: linkField
                width: parent.width
                variant: Md.Outlined
                label: qsTr("粘贴邀请链接")
                gapColor: Theme.color.surfaceContainerHigh
                error: text.trim() !== "" && dialog.linkKind !== TogetherState.Invite
                supportingText: text.trim() === "" || dialog.linkKind === TogetherState.Invite
                                ? qsTr("加入后，你的播放队列会换成房间的歌")
                                : dialog.linkKind === TogetherState.OwnRoom
                                  ? qsTr("这是你自己的房间")
                                  : qsTr("不是一起听的邀请链接")
                Keys.onReturnPressed: dialog.join()
                Keys.onEnterPressed: dialog.join()
            }
            Item {
                width: parent.width
                height: joinButton.height

                Button {
                    id: joinButton
                    anchors.right: parent.right
                    text: qsTr("加入")
                    variant: Md.Tonal
                    enabled: dialog.linkKind === TogetherState.Invite
                    onClicked: dialog.join()
                }
            }
        }
    }

    // ---- creating, joining or leaving ----
    Column {
        width: parent.width
        spacing: 16
        visible: dialog.busy
        topPadding: 8
        bottomPadding: 8

        LoadingIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: dialog.busy && dialog.visible
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: dialog.state === TogetherState.Leaving ? qsTr("正在离开…")
                  : dialog.together.resuming ? qsTr("正在恢复一起听…")
                  : dialog.together.joining ? qsTr("正在加入…") : qsTr("正在创建房间…")
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodyLarge.font
        }
    }

    // ---- in a room ----
    Column {
        width: parent.width
        spacing: 20
        visible: dialog.inRoom

        Column {
            width: parent.width
            spacing: 8

            Row {
                spacing: -12

                Repeater {
                    model: dialog.together.members
                    delegate: Column {
                        id: member
                        required property var modelData
                        spacing: 4

                        Rectangle {
                            // A ring of the dialog's color separates the overlapping avatars.
                            width: 52
                            height: 52
                            radius: 26
                            color: Theme.color.surfaceContainerHigh

                            CoverArt {
                                anchors.centerIn: parent
                                width: 48
                                height: 48
                                radius: 24
                                coverKey: member.modelData.avatarKey
                                glyph: Symbols.person
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: member.modelData.host
                            text: qsTr("房主")
                            color: Theme.color.onSurfaceVariant
                            font: Theme.type.labelSmall.font
                        }
                    }
                }
            }
            Text {
                width: parent.width
                text: dialog.together.partnerName === "" ? qsTr("等好友加入")
                      : qsTr("你和 %1 在一起听").arg(dialog.together.partnerName)
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: Theme.type.titleMedium.font
            }
            Row {
                spacing: 8

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: dialog.together.connected ? Theme.color.primary : Theme.color.error
                }
                Text {
                    text: dialog.together.connected ? qsTr("已连接") : qsTr("正在重新连接…")
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.bodyMedium.font
                }
            }
        }

        Column {
            width: parent.width
            spacing: 8
            visible: dialog.together.shareLink !== ""

            Text {
                text: qsTr("邀请链接")
                color: Theme.color.onSurface
                font: Theme.type.titleSmall.font
            }
            Rectangle {
                width: parent.width
                height: Math.max(56, linkText.implicitHeight + 24)
                radius: Theme.shape.medium
                color: Theme.color.surfaceContainerHighest

                SelectableText {
                    id: linkText
                    anchors.left: parent.left
                    anchors.right: copyButton.left
                    anchors.leftMargin: 16
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: dialog.together.shareLink
                    wrapMode: TextEdit.WrapAnywhere
                    font: Theme.type.bodyMedium.font
                    activeFocusOnTab: true
                }
                IconButton {
                    id: copyButton
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Symbols.content_copy
                    text: qsTr("复制邀请链接")
                    onClicked: dialog.together.copyLink()
                }
            }
            Text {
                width: parent.width
                text: qsTr("好友用网易云音乐 App 或 Cirrus 打开")
                wrapMode: Text.Wrap
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodySmall.font
            }
        }
    }
}
