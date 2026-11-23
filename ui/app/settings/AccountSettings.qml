pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation
import CirrusMusic

// The account: who is signed in, and signing in or out.
SettingsGroup {
    id: group

    required property AppShell shell

    title: qsTr("账号")
    glyph: Symbols.account_circle
    hue: "cyan"

    SettingRow {
        text: App.session.signedIn ? App.session.nickname : qsTr("未登录")
        supportingText: App.session.signedIn ? qsTr("网易云音乐账号") : ""
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
        leading: Component {
            CoverArt {
                width: 40
                height: 40
                radius: width / 2
                glyph: Symbols.person
                coverKey: App.session.avatarKey
            }
        }
        trailing: Component {
            Button {
                variant: App.session.signedIn ? Md.Outlined : Md.Filled
                glyph: App.session.signedIn ? Symbols.logout : Symbols.login
                text: App.session.signedIn ? qsTr("退出登录") : qsTr("登录")
                onClicked: App.session.signedIn ? App.session.signOut() : group.shell.openLogin()
            }
        }
    }
    SettingRow {
        text: App.player.biliStatus === 1 ? App.player.biliAccountName
              : App.player.biliStatus === 2 ? qsTr("正在读取或校验")
              : App.player.biliStatus === 3 ? qsTr("Cookie 格式无效")
              : App.player.biliStatus === 4 ? qsTr("网络校验失败")
              : App.player.biliStatus === 5 ? qsTr("系统密钥环不可用，未保存更改")
              : qsTr("未登录")
        supportingText: qsTr("Bilibili账号")
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
        leading: Component {
            CoverArt {
                width: 40
                height: 40
                radius: width / 2
                glyph: Symbols.person
            }
        }
        trailing: Component {
            Button {
                variant: App.player.biliStatus === 1 ? Md.Outlined : Md.Filled
                glyph: App.player.biliStatus === 1 ? Symbols.logout : Symbols.login
                text: App.player.biliStatus === 1 ? qsTr("退出登录") : qsTr("登录")
                enabled: App.player.biliStatus !== 2
                onClicked: App.player.biliStatus === 1 ? clearCookieDialog.open() : cookieDialog.open()
            }
        }
    }
    ActionSetting {
        // A failed validation does not establish whether a saved Cookie remains.
        shown: App.player.biliStatus !== 1
        text: qsTr("Bilibili 登录凭证")
        actionText: qsTr("清除凭证")
        actionEnabled: App.player.biliStatus !== 2
        onTriggered: clearCookieDialog.open()
    }
    Dialog {
        id: cookieDialog
        title: qsTr("粘贴 Bilibili Cookie")
        supportingText: qsTr("Cookie 等同登录凭证，请勿分享。只保存在系统密钥环，不保存到普通设置。")
        acceptText: qsTr("保存并校验")
        rejectText: qsTr("取消")
        acceptEnabled: App.player.biliStatus !== 2 && cookieInput.text.trim().length > 0
        initialFocusItem: cookieInput
        onAccepted: {
            App.player.saveBiliCookie(cookieInput.text);
            cookieInput.clear();
        }
        onRejected: cookieInput.clear()
        onAboutToShow: cookieInput.clear()
        TextField {
            id: cookieInput
            width: parent.width
            label: qsTr("Cookie")
            echoMode: TextInput.Password
            maximumLength: 16384
        }
    }
    Dialog {
        id: clearCookieDialog
        title: qsTr("清除 Bilibili Cookie？")
        supportingText: qsTr("删除系统密钥环中的 Cookie，之后以未登录状态请求音源。")
        acceptText: qsTr("清除")
        rejectText: qsTr("取消")
        acceptEnabled: App.player.biliStatus !== 2
        onAccepted: App.player.clearBiliCookie()
    }
}
