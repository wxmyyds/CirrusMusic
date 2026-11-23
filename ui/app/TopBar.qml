pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The app's top bar, in the window's title bar row (docs/ui/pages/shell.md §1): over
// the rail the rail toggle and the app's name (the logo goes before it once there is
// one), then, on the top of the content panel, back and forward, the search field
// (centred on the panel) and on the right the app's own actions. Window buttons, when the window has them, come after the actions (the
// title bar row adds them); the actions are never hidden. Empty space moves the
// window (the title bar row handles that under this item).
Item {
    id: bar

    required property AppShell shell
    readonly property alias searchField: field
    // App actions at the right end, e.g. full screen later.
    property alias actions: actionRow.data

    // Over the rail: as wide as the rail beside the panel.
    Item {
        id: lead
        width: bar.shell.navWidth - bar.x
        height: bar.height

        IconButton {
            x: 28
            anchors.verticalCenter: parent.verticalCenter
            glyph: bar.shell.railExpanded ? Symbols.menu_open : Symbols.menu
            text: bar.shell.railExpanded ? qsTr("收起导航栏") : qsTr("展开导航栏")
            onClicked: bar.shell.toggleRail()
        }
        // The app's name; the logo will go in front of it.
        Text {
            x: 80
            width: Math.max(0, lead.width - x - 16)
            anchors.verticalCenter: parent.verticalCenter
            opacity: Theme.motion.segment((lead.width - 96) / 176, 0.6, 1)
            visible: opacity > 0
            text: qsTr("Cirrus")
            elide: Text.ElideRight
            color: Theme.color.onSurface
            font: Theme.type.titleMedium.font
        }
    }

    Row {
        id: history
        x: lead.width + 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        IconButton {
            enabled: bar.shell.canGoBack
            glyph: Symbols.arrow_back
            text: qsTr("后退（Alt+←）")
            onClicked: bar.shell.back()
        }
        IconButton {
            enabled: bar.shell.canGoForward
            glyph: Symbols.arrow_forward
            text: qsTr("前进（Alt+→）")
            onClicked: bar.shell.forward()
        }
    }

    SearchField {
        id: field
        // Centred on the panel below where there is room, else after back and forward.
        readonly property real minX: history.x + history.width + 12
        readonly property real maxRight: Math.min(actionRow.x, bar.shell.panelRight - bar.x,
                                                  pageActions.visible ? pageActions.x : Infinity)
                                         - 16
        readonly property real panelCenter: bar.shell.panelCenter
        x: Math.max(minX, Math.min(maxRight - width, panelCenter - width / 2))
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, Math.min(560, maxRight - minX))
        history: bar.shell.searchModel.history
        hotKeywords: bar.shell.searchModel.hotKeywords
        suggestions: bar.shell.searchModel.suggestions
        onTextEdited: {
            if (!bar.shell.searching && text.trim() !== "")
                bar.shell.openSearch();
        }
        onSubmitted: value => bar.shell.search(value)
        onOpened: (kind, key) => bar.shell.openCollection(kind, key)
        onHistoryRemoved: value => bar.shell.searchModel.removeHistory(value)
        onHistoryCleared: bar.shell.searchModel.clearHistory()
        onDismissed: bar.shell.focusContent()
    }

    // At the right end of the content panel, before the side player: the page's own link
    // (pages/share.md §3).
    Row {
        id: pageActions
        x: Math.min(actionRow.x, bar.shell.panelRight - bar.x) - width - 8
        anchors.verticalCenter: parent.verticalCenter
        visible: bar.shell.pageShare !== null
        IconButton {
            glyph: Symbols.link
            text: qsTr("复制链接（Ctrl+Shift+C）")
            onClicked: bar.shell.copyPageLink()
        }
    }

    Row {
        id: actionRow
        x: bar.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        // Over the docked side player's top: its own buttons (the row lies over it).
        IconButton {
            visible: bar.shell.sideShown
            glyph: Symbols.open_in_full
            text: qsTr("打开播放页")
            onClicked: bar.shell.playerRequested()
        }
        IconButton {
            visible: bar.shell.sideShown
            glyph: Symbols.right_panel_close
            text: qsTr("变回悬浮播放条")
            onClicked: bar.shell.setDocked(false)
        }
    }
}
