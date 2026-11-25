import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// Overlay scroll bar: a 4 px bar that widens to 8 px under the pointer and fades
// out 1 s after scrolling stops. A vertical one also fixes its Flickable's mouse wheel on
// Qt 6.12 under Wayland (WheelFilter), so every scroll view gets that.
//   ScrollBar.vertical: ScrollBar {}
T.ScrollBar {
    id: control

    readonly property bool expanded: (hovered || pressed) && interactive
    readonly property bool shown: policy === T.ScrollBar.AlwaysOn
                                  || (active || hovered) && size < 1.0

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: 2
    visible: policy !== T.ScrollBar.AlwaysOff
    minimumSize: orientation === Qt.Horizontal ? height / width : width / height

    // Qt's own wheel scrolling, 120 px a notch at the usual 3 lines (Qt's 72 px felt slow,
    // 2026-11-14) times the user's Wheel.speed, and wheel notches stay notches on Qt 6.12
    // under Wayland (WheelFilter).
    WheelFilter {
        target: control.orientation === Qt.Vertical ? control.parent as Flickable : null
        scale: 5 / 3 * Wheel.speed
    }

    Spring {
        id: thickness
        target: control.expanded ? 8 : 4
        spec: Theme.motion.fastSpatial
        threshold: 0.05
    }

    contentItem: Item {
        implicitWidth: 8
        implicitHeight: 8
        opacity: 0

        Rectangle {
            readonly property bool vertical: control.orientation === Qt.Vertical
            anchors.right: vertical ? parent.right : undefined
            anchors.bottom: vertical ? undefined : parent.bottom
            width: vertical ? Math.max(1, thickness.value) : parent.width
            height: vertical ? parent.height : Math.max(1, thickness.value)
            radius: Math.min(width, height) / 2
            color: control.pressed ? ColorScope.scheme.onSurface
                                   : Qt.alpha(ColorScope.scheme.onSurfaceVariant,
                                              control.expanded ? 0.9 : 0.6)
        }
    }

    states: State {
        name: "shown"
        when: control.shown
        PropertyChanges {
            control.contentItem.opacity: 1
        }
    }
    transitions: [
        Transition {
            to: "shown"
            NumberAnimation {
                target: control.contentItem
                property: "opacity"
                duration: Theme.motion.fastEffects.duration
            }
        },
        Transition {
            from: "shown"
            SequentialAnimation {
                PauseAnimation {
                    duration: 1000
                }
                NumberAnimation {
                    target: control.contentItem
                    property: "opacity"
                    to: 0
                    duration: Theme.motion.slowEffects.duration
                }
            }
        }
    ]
}
