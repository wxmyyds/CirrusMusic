pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// A keyboard shortcut's key as a keycap that records a new one (docs/ui/pages/shortcuts.md
// §3). Clicking it, Space or Enter starts recording: the next key goes to `recorded`, and
// no shortcut fires while it listens. Esc, Tab and a click elsewhere end it with nothing
// (`recording` turns false), Backspace or Delete with `cleared`. The owner decides what a
// recorded key does and ends recording (`recording = false`) or lets it listen on (a key
// it can't use).
// With `glyph` and no `text` it is an outlined button for a key not set yet ("+").
//   ShortcutKey { text: "Ctrl+P"; onRecorded: (key, modifiers) => …; onCleared: … }
//   ShortcutKey { glyph: Symbols.add; hint: qsTr("添加按键"); onRecorded: … }
T.Button {
    id: control

    property bool recording: false
    property string glyph
    // Shown while recording.
    property string prompt: qsTr("按下快捷键…")
    // A tooltip, for a cap with only a glyph.
    property string hint

    readonly property bool outlined: text === "" && !recording
    readonly property color contentColor: !enabled
        ? Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContent)
        : Theme.mix(ColorScope.scheme.onSurfaceVariant, ColorScope.scheme.onPrimaryContainer,
                    listening.value)

    signal recorded(int key, int modifiers)
    signal cleared

    function start(): void {
        control.forceActiveFocus(Qt.OtherFocusReason);
        recording = true;
    }
    function cancel(): void {
        recording = false;
    }

    implicitWidth: Math.max(32, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: 32
    leftPadding: outlined ? 4 : 12
    rightPadding: leftPadding
    font: Theme.type.labelLarge.font
    focusPolicy: Qt.StrongFocus
    Accessible.name: recording ? prompt : text !== "" ? text : hint

    onClicked: start()
    onActiveFocusChanged: {
        if (!activeFocus)
            cancel();
    }

    // While listening every key is the recording's, before any shortcut.
    Keys.onShortcutOverride: event => event.accepted = control.recording
    Keys.onPressed: event => {
        const command = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier);
        if (!control.recording) {
            event.accepted = !command && (event.key === Qt.Key_Return
                                          || event.key === Qt.Key_Enter);
            if (event.accepted)
                control.start();
            return;
        }
        event.accepted = true;
        switch (event.key) {
        case Qt.Key_Escape:
            control.cancel();
            return;
        case Qt.Key_Tab:
        case Qt.Key_Backtab:
            // Moves the focus on (and so cancels); with Ctrl it is a key to record.
            if (!command) {
                event.accepted = false;
                return;
            }
            break;
        case Qt.Key_Backspace:
        case Qt.Key_Delete:
            if (!command && !(event.modifiers & Qt.ShiftModifier)) {
                control.recording = false;
                control.cleared();
                return;
            }
            break;
        case Qt.Key_Shift:
        case Qt.Key_Control:
        case Qt.Key_Alt:
        case Qt.Key_AltGr:
        case Qt.Key_Meta:
        case Qt.Key_Super_L:
        case Qt.Key_Super_R:
            return; // waits for the key they go with
        }
        control.recorded(event.key, event.modifiers);
    }

    Spring {
        id: listening
        target: control.recording ? 1 : 0
        spec: Theme.motion.defaultEffects
    }

    contentItem: Item {
        implicitWidth: control.recording || control.text !== "" ? label.implicitWidth : 24
        implicitHeight: 24

        Text {
            id: label
            anchors.centerIn: parent
            visible: !control.outlined
            text: control.recording ? control.prompt : control.text
            font: control.font
            color: control.contentColor
        }
        Icon {
            anchors.centerIn: parent
            visible: control.outlined
            glyph: control.glyph
            size: 18
            color: control.contentColor
        }
    }

    background: Rectangle {
        implicitWidth: 32
        implicitHeight: 32
        radius: Theme.shape.small
        color: !control.enabled
            ? Qt.alpha(ColorScope.scheme.onSurface,
                       control.outlined ? 0 : Theme.state.disabledContainer)
            : control.outlined
              ? Qt.alpha(ColorScope.scheme.primaryContainer, listening.value)
              : Theme.mix(ColorScope.scheme.surfaceContainerHighest,
                          ColorScope.scheme.primaryContainer, listening.value)
        border.width: control.outlined ? 1 : 0
        border.color: control.enabled ? ColorScope.scheme.outlineVariant
                                      : Qt.alpha(ColorScope.scheme.onSurface,
                                                 Theme.state.disabledContainer)

        StateLayer {
            anchors.fill: parent
            radius: parent.radius
            color: control.contentColor
            hovered: control.hovered && control.enabled && !control.recording
            pressed: control.down
            focused: control.visualFocus && !control.recording
            pressX: control.pressed ? control.pressX : width / 2
            pressY: control.pressed ? control.pressY : height / 2
        }
        FocusRing {
            shown: control.visualFocus
            containerRadius: parent.radius
        }
    }

    // A click anywhere else ends the recording; the click itself goes on (not accepted).
    Loader {
        active: control.recording
        sourceComponent: MouseArea {
            parent: control.T.Overlay.overlay
            anchors.fill: parent
            z: 1000
            onPressed: mouse => {
                mouse.accepted = false;
                control.cancel();
            }
        }
    }

    Loader {
        active: control.hint !== "" && control.text === "" && control.hovered
        sourceComponent: ToolTip {
            parent: control
            text: control.hint
            visible: control.hovered && !control.pressed && !control.recording
        }
    }

    // A hand pointer wherever the control shows hover (controls.md §2).
    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
}
