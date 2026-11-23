pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// The keyboard shortcuts, read only. The list is written here, next to nothing that
// defines them: when adding a Shortcut elsewhere, add its line (settings.md §4).
SettingsGroup {
    id: group

    // [{ text, keys: [key, ...] }]; alternative keys each get a cap, joined by "／".
    readonly property var shortcuts: [{
            text: qsTr("播放／暂停"),
            keys: ["Ctrl+P"]
        }, {
            text: qsTr("上一首／下一首"),
            keys: ["Ctrl+←", "Ctrl+→"]
        }, {
            text: qsTr("音量加／减"),
            keys: ["Ctrl+↑", "Ctrl+↓"]
        }, {
            text: qsTr("喜欢正在播放的歌曲"),
            keys: ["Ctrl+L"]
        }, {
            text: qsTr("复制链接"),
            keys: ["Ctrl+Shift+C"]
        }, {
            text: qsTr("搜索"),
            keys: ["Ctrl+K", "Ctrl+F"]
        }, {
            text: qsTr("后退／前进"),
            keys: ["Alt+←", "Alt+→"]
        }, {
            text: qsTr("切换标签"),
            keys: ["Ctrl+PgUp", "Ctrl+PgDown"]
        }, {
            text: qsTr("刷新首页"),
            keys: ["F5"]
        }, {
            text: qsTr("听歌统计的上一段／下一段"),
            keys: ["[", "]"]
        }, {
            text: qsTr("设置"),
            keys: ["Ctrl+,"]
        }, {
            text: qsTr("退出"),
            keys: ["Ctrl+Q"]
        }]

    // A key as printed on the keyboard.
    component Keycap: Rectangle {
        property alias text: label.text
        width: Math.max(28, label.implicitWidth + 16)
        height: 28
        radius: Theme.shape.small
        color: ColorScope.scheme.surfaceContainerHighest
        Text {
            id: label
            anchors.centerIn: parent
            color: ColorScope.scheme.onSurfaceVariant
            font: Theme.type.labelLarge.font
        }
    }

    title: qsTr("键盘快捷键")
    glyph: Symbols.keyboard
    hue: "grey"

    Repeater {
        model: group.shortcuts
        delegate: SettingRow {
            id: shortcutRow
            required property var modelData
            text: modelData.text
            focusPolicy: Qt.NoFocus
            hoverEnabled: false
            Accessible.description: modelData.keys.join(qsTr("，"))
            trailing: Component {
                Row {
                    spacing: 8
                    Repeater {
                        model: shortcutRow.modelData.keys
                        delegate: Row {
                            id: key
                            required property int index
                            required property string modelData
                            spacing: 8
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: key.index > 0
                                text: qsTr("或")
                                color: ColorScope.scheme.onSurfaceVariant
                                font: Theme.type.bodySmall.font
                            }
                            Keycap {
                                text: key.modelData
                            }
                        }
                    }
                }
            }
        }
    }
    SettingRow {
        text: qsTr("列表和页面里")
        supportingText: qsTr("↑／↓ 移动，Enter 打开，←／→ 切换标签和选项，菜单键打开菜单")
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
    }
}
