pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Cirrus.Theme
import Cirrus.Controls

// A cover by key (`image://cover/<key>`) with rounded corners over a placeholder.
// A new cover replaces the shown one only once decoded, with a cross-fade
// (motion-choreography.md §4.1). Two layers at most: when a cover arrives during a
// fade, the more visible layer becomes the base. The rounded corners come from a shader
// that samples the image itself (shaders/cover.frag); the software renderer has no
// shaders, so covers stay square there. `shape` cuts the cover to an MD3E shape instead
// of a rounded rectangle (decorative covers only: it crops the corners), through an
// offscreen layer and a mask.
//   CoverArt { coverKey: key; shape: MaterialShape.Cookie9Sided }
Item {
    id: root

    property string coverKey
    property real radius: Theme.shape.small
    // MaterialShape.* (import M3Shapes), or -1 for the rounded rectangle.
    property int shape: -1
    property string glyph: Symbols.music_note
    // Decode size; by default the item's size in device pixels, rounded up to a step.
    // With the exact size, every pixel of a resize (a window edge being dragged, a
    // pane sliding) decoded and cached every visible cover again, at a new size.
    property size sourceSize: Qt.size(decodeLength(width), decodeLength(height))
    // A cover that arrives shows at once instead of fading in (the flying cover, which
    // must not start from the placeholder).
    property bool immediate: false
    // The cover is decoded and fully shown (no fade or load pending).
    readonly property bool ready: pending === null && !fade.running && base.status === Image.Ready

    readonly property bool masked: GraphicsInfo.api !== GraphicsInfo.Software
                                   && GraphicsInfo.api !== GraphicsInfo.Unknown
    // Only a shape needs the offscreen layer (an MD3E shape mask); a page of covers each
    // with a layer and a mask texture of their own held two offscreen textures per cover.
    readonly property bool shaped: masked && shape >= 0
    // Layer showing the current cover, the other one fades in over it.
    property Image base: first
    // The layer loading a new cover, revealed once it is done. Only it: the other layer
    // also reports status changes (a resize reloads it, or re-reports an empty one).
    property Image pending: null

    function show(): void {
        pending = null;
        const url = coverKey !== "" ? "image://cover/" + encodeURIComponent(coverKey) : "";
        if (fade.running) {
            fade.stop();
            if (second.opacity > first.opacity)
                base = second;
            else
                base = first;
        }
        const incoming = base === first ? second : first;
        base.opacity = 1;
        base.z = 1;
        incoming.z = 2;
        incoming.opacity = 0;
        incoming.source = url;
        // An Image not complete yet (a delegate being created) starts loading later and
        // is still Null here: its status change reveals it.
        if (incoming.status !== Image.Loading && (url === "" || incoming.status !== Image.Null))
            reveal(incoming);
        else
            pending = incoming;
    }

    // Called once the incoming layer has loaded or failed: fades it in, or fades the
    // shown cover out to the placeholder when there is nothing to show.
    function reveal(layer: Image): void {
        if (immediate) {
            settle();
            return;
        }
        const loaded = layer.status === Image.Ready;
        if (!loaded && base.status !== Image.Ready) {
            settle();
            return;
        }
        fade.target = loaded ? layer : base;
        fade.to = loaded ? 1 : 0;
        fade.restart();
    }

    // The incoming layer becomes the base; the old one is released.
    function settle(): void {
        const incoming = base === first ? second : first;
        base.opacity = 0;
        base.source = "";
        // Still loading here only when it is reloading at a new size (retained).
        incoming.opacity = incoming.status === Image.Ready || incoming.status === Image.Loading ? 1 : 0;
        base = incoming;
    }

    function decodeLength(length: real): int {
        const pixels = Math.ceil(length * Screen.devicePixelRatio);
        const step = pixels <= 128 ? 32 : 64;
        return Math.ceil(pixels / step) * step;
    }

    onCoverKeyChanged: show()
    Component.onCompleted: show()

    component Layer: Image {
        anchors.fill: parent
        sourceSize: root.sourceSize
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        // A new size (the item resizing) reloads it: keep the old pixels meanwhile.
        retainWhileLoading: true
        opacity: 0
        // With shaders a Rounded draws it; inside a shape's layer it draws itself.
        visible: !root.masked || root.shaped
        onStatusChanged: {
            if (this === root.pending && status !== Image.Loading) {
                root.pending = null;
                root.reveal(this);
            }
        }
    }

    // A layer's image with the rounded corners, from its texture (PreserveAspectCrop: the
    // painted size over the item's).
    component Rounded: ShaderEffect {
        required property Image image
        readonly property Item source: image
        readonly property size boxSize: Qt.size(width, height)
        readonly property real radius: root.radius
        readonly property size crop: Qt.size(Math.max(1, image.paintedWidth) / Math.max(1, width),
                                             Math.max(1, image.paintedHeight) / Math.max(1, height))
        anchors.fill: parent
        visible: root.masked && !root.shaped && image.status === Image.Ready && image.opacity > 0
        z: image.z
        opacity: image.opacity
        fragmentShader: Qt.resolvedUrl("shaders/cover.frag.qsb")
    }

    Item {
        id: content
        anchors.fill: parent
        layer.enabled: root.shaped
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: shapeMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }

        Rectangle {
            anchors.fill: parent
            radius: root.shaped ? 0 : root.radius
            color: ColorScope.scheme.surfaceContainerHighest
            Icon {
                anchors.centerIn: parent
                glyph: root.glyph
                size: Math.round(Math.min(root.width, root.height) * 0.4)
                color: ColorScope.scheme.onSurfaceVariant
            }
        }
        Layer {
            id: first
        }
        Layer {
            id: second
        }
        Rounded {
            image: first
        }
        Rounded {
            image: second
        }
    }

    Loader {
        id: shapeMask
        anchors.fill: parent
        active: root.shaped
        visible: false
        layer.enabled: active
        sourceComponent: MorphShape {
            shape: root.shape
            // Any opaque color: the mask reads its alpha.
            color: ColorScope.scheme.scrim
        }
    }

    NumberAnimation {
        id: fade
        property: "opacity"
        duration: (Theme.motion.reduced ? Theme.motion.fastEffects : Theme.motion.defaultEffects).duration
        easing: (Theme.motion.reduced ? Theme.motion.fastEffects : Theme.motion.defaultEffects).easing
        onFinished: root.settle()
    }
}
