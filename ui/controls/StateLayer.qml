pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// MD3 state layer drawn over a control's container: a hover/focus/drag overlay in
// the content color plus a press ripple. The ripple is a shader; under the software
// renderer it is replaced by a flat pressed overlay.
Item {
    id: root

    property color color: ColorScope.scheme.onSurface
    property bool hovered
    property bool pressed
    property bool focused
    property bool dragged
    property real pressX: width / 2
    property real pressY: height / 2

    property real radius
    property real topLeftRadius: radius
    property real topRightRadius: radius
    property real bottomRightRadius: radius
    property real bottomLeftRadius: radius

    // Asked on the first press only: hundreds of layers, most never pressed, each kept
    // a GraphicsInfo and a ripple shader effect of their own.
    function rippleSupported(): bool {
        return GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown;
    }
    property bool pressedOnce: false

    Accessible.ignored: true

    Spring {
        id: overlay
        target: root.dragged ? Theme.state.dragged
              : root.pressed && !root.rippleSupported() ? Theme.state.pressed
              : root.focused ? Theme.state.focus
              : root.hovered ? Theme.state.hover : 0
        spec: Theme.motion.fastEffects
        threshold: 0.002
    }

    Rectangle {
        anchors.fill: parent
        visible: overlay.value > 0
        color: root.color
        opacity: overlay.value
        topLeftRadius: root.topLeftRadius
        topRightRadius: root.topRightRadius
        bottomRightRadius: root.bottomRightRadius
        bottomLeftRadius: root.bottomLeftRadius
    }

    onPressedChanged: {
        if (!rippleSupported())
            return;
        if (pressed) {
            pressedOnce = true; // loads the ripple, synchronously
            (rippleLoader.item as Ripple)?.start(pressX, pressY);
        } else {
            (rippleLoader.item as Ripple)?.release();
        }
    }

    Loader {
        id: rippleLoader
        anchors.fill: parent
        active: root.pressedOnce
        sourceComponent: Ripple {
            rippleColor: root.color
            radii: Qt.vector4d(root.topLeftRadius, root.topRightRadius, root.bottomRightRadius,
                               root.bottomLeftRadius)
        }
    }

    component Ripple: ShaderEffect {
        id: ripple

        property color rippleColor
        property size boxSize: Qt.size(width, height)
        property vector4d radii
        property point center
        property real radius
        property real strength

        function start(x: real, y: real) {
            const far = Math.max(Math.hypot(x, y), Math.hypot(width - x, y),
                                 Math.hypot(x, height - y), Math.hypot(width - x, height - y));
            center = Qt.point(x, y);
            fadeOut.stop();
            grow.from = Math.max(width, height) * 0.15;
            grow.to = far + 10;
            grow.restart();
            fadeIn.restart();
        }

        function release() {
            fadeIn.stop();
            fadeOut.restart();
        }

        visible: strength > 0
        fragmentShader: Qt.resolvedUrl("shaders/ripple.frag.qsb")

        NumberAnimation {
            id: grow
            target: ripple
            property: "radius"
            duration: 225
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            id: fadeIn
            target: ripple
            property: "strength"
            to: Theme.state.pressed
            duration: 75
        }
        NumberAnimation {
            id: fadeOut
            target: ripple
            property: "strength"
            to: 0
            duration: 150
        }
    }
}
