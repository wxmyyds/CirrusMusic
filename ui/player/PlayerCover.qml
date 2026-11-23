pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The player's cover (docs/ui/pages/player.md §4): a soft shadow in the cover's own
// darkest color, a little smaller while paused, as if laid down (springy, the shadow
// shrinking with it), and under the pointer it tilts toward it (at most 6°) while a
// faint glare slides the other way. Without motion only the shadow stays.
Item {
    id: root

    required property PlayerPalette colors
    readonly property real radius: Theme.shape.medium
    // The drawn cover, scaled while paused: the shared element flies to its rect.
    readonly property Item art: drawn
    // The largest the cover gets, in device pixels.
    readonly property int decodeSize: Math.ceil(720 * Screen.devicePixelRatio)
    readonly property bool playing: App.player.status === Playback.Playing
                                    || App.player.status === Playback.Loading

    readonly property real maxTilt: 6
    readonly property bool tilts: App.settings.playerTilt && !Theme.motion.reduced
    // The pointer over the cover, -1 to 1 on each axis; 0 when away.
    readonly property point aim: tilts && hover.hovered
                                 ? Qt.point(Math.max(-1, Math.min(1, hover.point.position.x
                                                                     / width * 2 - 1)),
                                            Math.max(-1, Math.min(1, hover.point.position.y
                                                                     / height * 2 - 1)))
                                 : Qt.point(0, 0)

    HoverHandler {
        id: hover
    }
    Spring {
        id: tiltX
        target: root.aim.x
        spec: Theme.motion.defaultSpatial
    }
    Spring {
        id: tiltY
        target: root.aim.y
        spec: Theme.motion.defaultSpatial
    }

    Spring {
        id: lift
        target: root.playing || Theme.motion.reduced ? 1 : 0
        spec: Theme.motion.defaultSpatial
    }

    Item {
        id: drawn
        anchors.fill: parent
        scale: 0.86 + 0.14 * lift.value
        // The side under the pointer leans away (Rotation has perspective).
        transform: [
            Rotation {
                origin.x: drawn.width / 2
                origin.y: drawn.height / 2
                axis { x: 0; y: 1; z: 0 }
                angle: root.maxTilt * tiltX.value
            },
            Rotation {
                origin.x: drawn.width / 2
                origin.y: drawn.height / 2
                axis { x: 1; y: 0; z: 0 }
                angle: -root.maxTilt * tiltY.value
            }
        ]

        Shadow {
            anchors.fill: parent
            radius: root.radius
            offset.y: 8 + 16 * lift.value
            blur: 24 + 40 * lift.value
            spread: -4
            color: Qt.alpha(root.colors.shadow, (root.colors.dark ? 0.55 : 0.35)
                            * (0.6 + 0.4 * lift.value))
        }

        CoverArt {
            anchors.fill: parent
            radius: root.radius
            coverKey: App.player.current.coverKey
            // One decode at the largest size, not one per frame while it glides.
            sourceSize: Qt.size(root.decodeSize, root.decodeSize)
        }

        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 12
            width: sourceText.implicitWidth + 20
            height: sourceText.implicitHeight + 12
            radius: height / 2
            color: Qt.alpha("black", 0.65)
            Text {
                id: sourceText
                anchors.centerIn: parent
                text: App.player.provider === Playback.Bilibili ? "Bilibili" : qsTr("网易云")
                color: "white"
                font: Theme.type.labelLarge.font
            }
        }

        // The glare, opposite the pointer, as strong as the tilt.
        Shape {
            id: glare
            anchors.fill: parent
            visible: opacity > 0.01
            opacity: Math.min(1, Math.hypot(tiltX.value, tiltY.value))
            preferredRendererType: Shape.CurveRenderer

            // Drawn again after the window comes back from the tray (a Qt bug).
            ShapeRefresh {
                id: glareRefresh
                target: glare
            }

            ShapePath {
                startX: glareRefresh.offset
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: drawn.width * (0.5 - 0.5 * tiltX.value)
                    centerY: drawn.height * (0.5 - 0.5 * tiltY.value)
                    centerRadius: drawn.width * 0.8
                    focalX: centerX
                    focalY: centerY
                    GradientStop {
                        position: 0
                        color: Qt.alpha(root.colors.glare, 0.2)
                    }
                    GradientStop {
                        position: 1
                        color: Qt.alpha(root.colors.glare, 0)
                    }
                }
                PathRectangle {
                    width: drawn.width
                    height: drawn.height
                    radius: root.radius
                }
            }
        }
    }
}
