pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The keyboard shortcuts (docs/ui/pages/shortcuts.md): each action's keys as caps to
// record a new key on, "+" for a second one, ↺ back to its defaults. The actions and
// their defaults are ShortcutMap's (App.shortcuts); the keys lists and dialogs use
// themselves are fixed and listed at the end.
SettingsGroup {
    id: group

    // A key as printed on the keyboard (the fixed ones).
    component Keycap: Rectangle {
        property alias text: label.text
        width: Math.max(32, label.implicitWidth + 24)
        height: 32
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
        model: App.shortcuts.actions
        delegate: SettingRow {
            id: actionRow

            required property var modelData
            readonly property string actionId: modelData.id
            readonly property list<string> keys: App.shortcuts.keys[actionId] ?? []
            readonly property bool customized: keys.join("\n") !== modelData.defaults.join("\n")
            // The cap that listens (0, 1, or the "+" at keys.length), else -1.
            property int recordingIndex: -1
            // The last key pressed can't be a shortcut (it listens on).
            property bool refused: false
            // A recorded key another action has, waiting for 替换 or 取消.
            property string pending
            property int pendingIndex: -1
            readonly property string pendingOwner: pending !== ""
                ? App.shortcuts.owner(pending, actionId) : ""

            // A key the cap at `index` recorded; whether it listens on.
            function take(index: int, key: int, modifiers: int): bool {
                const sequence = App.shortcuts.sequenceOf(key, modifiers);
                if (sequence === "")
                    return true;
                if (App.shortcuts.reserved(sequence)) {
                    refused = true;
                    return true;
                }
                refused = false;
                if (keys[index] === sequence)
                    return false;
                if (App.shortcuts.owner(sequence, actionId) !== "") {
                    pending = sequence;
                    pendingIndex = index;
                    Qt.callLater(() => actionRow.focusRequested(-1));
                    return false;
                }
                App.shortcuts.assign(actionId, index, sequence);
                return false;
            }
            function resolve(replace: bool): void {
                const index = pendingIndex;
                if (replace)
                    App.shortcuts.assign(actionId, index, pending);
                pending = "";
                pendingIndex = -1;
                focusSlot(index);
            }
            // The focus to the cap at `index` (or the "+" before it), once it shows.
            function focusSlot(index: int): void {
                Qt.callLater(() => actionRow.focusRequested(Math.min(index, actionRow.keys.length)));
            }
            // A cap (its index) or 替换 (-1) takes the focus.
            signal focusRequested(int slot)

            text: modelData.text
            supportingText: pendingOwner !== ""
                ? qsTr("%1 已用于「%2」").arg(App.shortcuts.displayText(pending))
                                       .arg(App.shortcuts.title(pendingOwner))
                : refused ? qsTr("这个键不能用作快捷键")
                : recordingIndex >= 0 && recordingIndex < keys.length
                  ? qsTr("Esc 取消，Backspace 删除这个键")
                : recordingIndex >= 0 ? qsTr("Esc 取消") : ""
            // The caps take the focus and the clicks, not the row.
            focusPolicy: Qt.NoFocus
            hoverEnabled: false
            Accessible.description: keys.map(key => App.shortcuts.displayText(key))
                                        .join(qsTr("，"))

            trailing: Component {
                Row {
                    spacing: 8

                    // A cap per key the action may have (ShortcutMap::MaximumKeys), kept
                    // while keys change so the focus stays: the keys, then "+" for the next.
                    Repeater {
                        model: 2
                        delegate: ShortcutKey {
                            id: cap
                            required property int index
                            readonly property string sequence: actionRow.keys[index] ?? ""
                            anchors.verticalCenter: parent?.verticalCenter
                            visible: actionRow.pendingOwner === "" && index <= actionRow.keys.length
                            text: sequence !== "" ? App.shortcuts.displayText(sequence) : ""
                            glyph: Symbols.add
                            hint: index > 0 ? qsTr("添加备用按键") : qsTr("添加按键")
                            Accessible.description: actionRow.text
                            onRecordingChanged: {
                                if (recording) {
                                    actionRow.refused = false;
                                    actionRow.recordingIndex = index;
                                } else if (actionRow.recordingIndex === index) {
                                    actionRow.recordingIndex = -1;
                                    actionRow.refused = false;
                                }
                            }
                            onRecorded: (key, modifiers) => cap.recording =
                                actionRow.take(cap.index, key, modifiers)
                            onCleared: {
                                if (cap.sequence !== "")
                                    App.shortcuts.clear(actionRow.actionId, cap.index);
                            }
                            Connections {
                                target: actionRow
                                function onFocusRequested(slot: int) {
                                    if (slot === cap.index)
                                        cap.forceActiveFocus(Qt.TabFocusReason);
                                }
                            }
                        }
                    }
                    Button {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: actionRow.pendingOwner !== ""
                        variant: Md.Text
                        text: qsTr("取消")
                        onClicked: actionRow.resolve(false)
                    }
                    Button {
                        id: replaceButton
                        anchors.verticalCenter: parent.verticalCenter
                        visible: actionRow.pendingOwner !== ""
                        variant: Md.Tonal
                        text: qsTr("替换")
                        onClicked: actionRow.resolve(true)
                        Connections {
                            target: actionRow
                            function onFocusRequested(slot: int) {
                                if (slot === -1)
                                    replaceButton.forceActiveFocus(Qt.TabFocusReason);
                            }
                        }
                    }
                    // Kept in place when hidden, so the caps of all rows end in one line.
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        opacity: actionRow.customized ? 1 : 0
                        enabled: actionRow.customized && actionRow.pendingOwner === ""
                        glyph: Symbols.restart_alt
                        text: qsTr("恢复默认")
                        onClicked: {
                            App.shortcuts.reset(actionRow.actionId);
                            actionRow.focusSlot(0);
                        }
                    }
                }
            }
        }
    }
    ActionSetting {
        text: qsTr("全部恢复默认")
        actionText: qsTr("恢复")
        actionEnabled: App.shortcuts.customized
        onTriggered: resetDialog.open()
    }
    SettingRow {
        text: qsTr("列表和页面里")
        supportingText: qsTr("↑／↓ 移动，Enter 打开，←／→ 切换标签和选项，菜单键打开菜单")
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
    }
    SettingRow {
        text: qsTr("记下诊断时刻")
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
        Accessible.description: "Ctrl+Alt+Shift+D"
        trailing: Component {
            Keycap {
                text: "Ctrl+Alt+Shift+D"
            }
        }
    }

    Dialog {
        id: resetDialog
        title: qsTr("全部快捷键恢复默认？")
        supportingText: qsTr("改过的快捷键都会换回默认的按键。")
        acceptText: qsTr("恢复")
        rejectText: qsTr("取消")
        onAccepted: App.shortcuts.resetAll()
    }
}
