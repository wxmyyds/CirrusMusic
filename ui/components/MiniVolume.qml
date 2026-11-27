pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The mini player's volume: an icon button that follows the volume and opens a small
// popup above it (a mute button, a slider, the percentage), as the player page's does
// (ui/player/VolumeButton.qml, in the page's own colors). The wheel over the button
// changes the volume without opening it. In the popup ←/→ step 1%, ↑/↓ 5%, M mutes or
// restores, Esc closes.
IconButton {
    id: root

    // The volume asked for last; the player reports it a moment later.
    property real pending: -1
    readonly property real volume: pending >= 0 ? pending : App.player.volume
    // Volume before muting, to restore it.
    property real unmuted: 0.5

    function setVolume(value: real): void {
        pending = Math.max(0, Math.min(1, value));
        App.player.setVolume(pending);
        settled.restart();
    }
    function toggleMute(): void {
        if (volume > 0) {
            unmuted = volume;
            setVolume(0);
        } else {
            setVolume(unmuted > 0 ? unmuted : 0.5);
        }
    }

    glyph: volume <= 0 ? Symbols.volume_off : volume < 0.34 ? Symbols.volume_mute
         : volume < 0.67 ? Symbols.volume_down : Symbols.volume_up
    text: qsTr("音量")
    showToolTip: !bar.opened
    Accessible.description: Math.round(volume * 100) + "%"
    onClicked: bar.opened ? bar.close() : bar.open()

    Timer {
        id: settled
        interval: 1000
        onTriggered: root.pending = -1
    }

    WheelHandler {
        onWheel: event => root.setVolume(root.volume + (event.angleDelta.y > 0 ? 0.05 : -0.05))
    }

    T.Popup {
        id: bar

        x: Math.round((root.width - width) / 2)
        y: -height - 12
        padding: 8
        rightPadding: 16
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        focus: true
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.fastEffects.duration
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.motion.fastEffects.duration
            }
        }

        // Like the menus: a popup surface with a shadow.
        background: Item {
            Elevation {
                anchors.fill: parent
                level: Theme.elevation.level2
                radius: Theme.shape.large
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.shape.large
                color: ColorScope.scheme.surfaceContainerLow
            }
        }

        contentItem: Row {
            spacing: 4
            Keys.onUpPressed: root.setVolume(root.volume + 0.05)
            Keys.onDownPressed: root.setVolume(root.volume - 0.05)
            // M mutes here, before a single-key shortcut.
            Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_M
                && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            Keys.onPressed: event => {
                if (event.key === Qt.Key_M) {
                    root.toggleMute();
                    event.accepted = true;
                }
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                glyph: root.glyph
                text: root.volume > 0 ? qsTr("静音") : qsTr("取消静音")
                onClicked: root.toggleMute()
            }
            Slider {
                anchors.verticalCenter: parent.verticalCenter
                width: 180
                focus: true
                from: 0
                to: 1
                stepSize: 0.01
                value: root.volume
                Accessible.name: qsTr("音量")
                onMoved: root.setVolume(value)
                WheelHandler {
                    onWheel: event => root.setVolume(root.volume + (event.angleDelta.y > 0 ? 0.05 : -0.05))
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 40
                horizontalAlignment: Text.AlignRight
                text: Math.round(root.volume * 100)
                color: ColorScope.scheme.onSurfaceVariant
                font: Fonts.tabular(Theme.type.labelLarge.font)
            }
        }
    }
}
