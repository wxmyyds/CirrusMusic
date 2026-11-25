pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// Top-level window with the Cirrus frame (docs/ui/README.md §6.1). `titleBar` picks
// the window decorations:
// - Custom: drawn by the app (shadow and rounded corners on Wayland, the system's
//   rounded corners on Windows 11, resize bands, window buttons); macOS keeps the
//   traffic lights over an expanded client area.
// - System: native decorations.
// - Hidden: none, for tiling window managers: no buttons, shadow or corners; the
//   title bar row still moves the window and resize bands stay.
// The app's own content for the title bar row (`titleContent`: the app's top bar,
// docs/ui/pages/shell.md) stays there in every mode; without it the row collapses
// unless it has window controls. `showTitle` shows `title` in the row (the gallery;
// the app draws its name itself). Default children fill the area below the row,
// over `surfaceColor`. `overlay` children cover the whole window, title bar row
// included (the player page).
//   AppWindow { title: qsTr("Cirrus"); Page { anchors.fill: parent } }
Window {
    id: window

    property int titleBar: TitleBarMode.Custom
    default property alias content: body.data
    property alias titleContent: titleBarItem.content
    property alias overlay: overlayItem.data
    property alias frame: windowFrame
    property bool showTitle: true
    property color surfaceColor: Theme.color.surface
    // The default children start at the window's top, under the title bar row (which
    // stays above them and keeps moving the window); they keep its height clear
    // where it matters (`titleHeight`).
    property bool titleOverContent: false
    property alias titleContentHeight: titleBarItem.contentHeight
    property alias titleTopInset: titleBarItem.topInset
    readonly property real titleHeight: titleBarItem.height

    readonly property bool macOS: Qt.platform.os === "osx"
    readonly property bool customFrame: titleBar === TitleBarMode.Custom && !macOS
    readonly property bool maximized: visibility === Window.Maximized
                                      || visibility === Window.FullScreen
    // Children reaching the window's bottom corners round theirs with this.
    readonly property real cornerRadius: windowFrame.margin > 0 ? Theme.shape.medium : 0

    // Changes the title bar mode by recreating the native window (hide, change flags,
    // show). Whether this works everywhere decides if the setting needs a restart.
    function setTitleBar(mode: int) {
        if (mode === titleBar)
            return;
        const shown = visible;
        visible = false;
        titleBar = mode;
        visible = shown;
    }

    minimumWidth: 480 + 2 * windowFrame.margin
    minimumHeight: 360 + 2 * windowFrame.margin
    // Frameless, the window still says it can be minimized and maximized and has a system
    // menu. Windows gives a window whose flags are spelled out only what they name (the
    // usual set comes with a bare Qt.Window alone): without the minimize style, clicking
    // the taskbar button of the active window does not minimize it, and Win+Down does
    // nothing.
    flags: titleBar === TitleBarMode.System ? Qt.Window
         : titleBar === TitleBarMode.Custom && macOS
           ? Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
         : Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint
           | Qt.WindowMinMaxButtonsHint
    // Translucent where the frame can have a shadow margin (Wayland): the alpha channel
    // is chosen when the native window is created, and the margin needs it. `surface`
    // paints the opaque part. Elsewhere opaque: there is no margin, and on Windows a
    // translucent frameless window may not get the system's rounded corners.
    color: windowFrame.translucent ? "transparent" : window.surfaceColor

    WindowFrame {
        id: windowFrame
        window: window
        clientSide: window.customFrame
        dark: Theme.dark
    }

    Shadow {
        anchors.fill: surface
        visible: windowFrame.margin > 0
        radius: window.cornerRadius
        offset.y: window.active ? 4 : 2
        blur: window.active ? 20 : 12
        color: Qt.alpha(Theme.color.shadow, window.active ? 0.4 : 0.24)
    }

    // The visible window. Children must keep clear of the rounded corners (only the
    // surface color is painted there) or round their own with `cornerRadius`.
    Rectangle {
        id: surface

        anchors.fill: parent
        anchors.margins: windowFrame.margin
        radius: window.cornerRadius
        color: window.surfaceColor

        TitleBar {
            id: titleBarItem

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            z: window.titleOverContent ? 1 : 0
            frame: windowFrame
            title: window.showTitle ? window.title : ""
            windowControls: window.titleBar !== TitleBarMode.System
            windowButtons: window.customFrame
            leadingInset: window.titleBar === TitleBarMode.Custom && window.macOS ? 72
                                                                                 : window.showTitle ? 16 : 0
        }

        Item {
            id: body

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: window.titleOverContent ? parent.top : titleBarItem.bottom
            anchors.bottom: parent.bottom
        }

        Item {
            id: overlayItem
            z: 2
            anchors.fill: parent
        }
    }

    // Hairline edge so the frame stays visible on dark backgrounds, where the
    // shadow is weak. Above the content, which never paints there anyway.
    Rectangle {
        anchors.fill: surface
        visible: windowFrame.margin > 0
        radius: window.cornerRadius
        color: "transparent"
        border.width: 1
        border.color: Qt.alpha(Theme.color.outlineVariant, 0.6)
    }

    // Frameless modes; inside the window only unless there is a shadow margin.
    ResizeHandles {
        anchors.fill: surface
        visible: (window.customFrame || window.titleBar === TitleBarMode.Hidden)
                 && !window.maximized
        outside: windowFrame.margin > 0 ? windowFrame.resizeMargin : 0
    }
}
