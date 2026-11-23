pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The play queue (docs/ui/pages/player.md §8) in a PlayerSheet: the whole queue in
// play order, opened at the playing song, which is the selected row. A click or Enter
// plays a row; the handle on the left drags it to another place (Alt+↑/↓ with the
// keyboard), the button on the right removes it (Delete). The context menu (also the
// menu key or Shift+F10) leads to the artist, the album, or adds to a playlist.
// Personal FM can't be edited.
FocusScope {
    id: root

    signal artistRequested(string key)
    signal albumRequested(string key)
    signal podcastRequested(string key) // a voice's podcast
    signal addToPlaylistRequested(string key, string title)

    readonly property int playing: App.player.queueIndex
    readonly property Item list: list
    readonly property bool editable: App.player.mode === Playback.Queue
    readonly property real rowHeight: 56

    // Dragging a row by its handle: the row, the pointer in the list, and the gap it
    // would go into (rows before which it lands; count for the end), -1 when none.
    property int dragRow: -1
    property real dragY: 0
    property int dropGap: -1

    function followDrag(): void {
        const at = list.indexAt(list.width / 2, list.contentY + dragY);
        if (at < 0) {
            dropGap = dragY < 0 || list.contentY + dragY < 0 ? 0 : list.count;
            return;
        }
        const item = list.itemAtIndex(at);
        const middle = item.y + item.height / 2;
        dropGap = list.contentY + dragY < middle ? at : at + 1;
    }
    function drop(): void {
        const from = dragRow, gap = dropGap;
        dragRow = -1;
        dropGap = -1;
        if (from < 0 || gap < 0 || gap === from || gap === from + 1)
            return;
        const to = gap > from ? gap - 1 : gap;
        App.player.moveQueue(from, to);
        list.currentIndex = to;
    }
    function moveBy(step: int): void {
        const from = list.currentIndex, to = from + step;
        if (!editable || from < 0 || to < 0 || to >= list.count)
            return;
        App.player.moveQueue(from, to);
        list.currentIndex = to;
        list.positionViewAtIndex(to, ListView.Contain);
    }

    // The playing song in view, near the top.
    function showPlaying(): void {
        if (playing >= 0) {
            list.currentIndex = playing;
            list.positionViewAtIndex(Math.max(0, playing - 1), ListView.Beginning);
        }
    }
    onPlayingChanged: if (!listHover.hovered && !list.moving)
                          Qt.callLater(showPlaying)
    onVisibleChanged: if (visible)
                          Qt.callLater(showPlaying)

    Text {
        x: 24
        visible: list.count === 0
        text: qsTr("播放队列为空")
        color: ColorScope.scheme.onSurfaceVariant
        font: Theme.type.bodyLarge.font
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        clip: true
        focus: true
        model: App.player.queue
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: count > 0
        highlightFollowsCurrentItem: false
        Accessible.role: Accessible.List
        Accessible.name: qsTr("播放队列")
        ScrollBar.vertical: ScrollBar {}

        onContentYChanged: Qt.callLater(loadVisible)
        onHeightChanged: Qt.callLater(loadVisible)
        onCountChanged: Qt.callLater(loadVisible)
        function loadVisible(): void {
            const first = Math.max(0, indexAt(0, contentY));
            App.player.ensureQueueLoaded(first - 20, first + Math.ceil(height / 56) + 20);
        }

        Keys.onReturnPressed: App.player.playAt(currentIndex)
        Keys.onEnterPressed: App.player.playAt(currentIndex)
        Keys.onDeletePressed: if (root.editable && currentIndex >= 0)
                                  App.player.removeAt(currentIndex)
        Keys.onPressed: event => {
            if (!(event.modifiers & Qt.AltModifier))
                return;
            if (event.key === Qt.Key_Up)
                root.moveBy(-1);
            else if (event.key === Qt.Key_Down)
                root.moveBy(1);
            else
                return;
            event.accepted = true;
        }

        // Rows slide aside when one moves or goes.
        displaced: Transition {
            NumberAnimation {
                property: "y"
                duration: Theme.motion.defaultSpatial.duration
                easing.type: Easing.OutCubic
            }
        }

        // Near the edges a drag scrolls the list.
        Timer {
            interval: 16
            repeat: true
            running: root.dragRow >= 0 && (root.dragY < 32 || root.dragY > list.height - 32)
            onTriggered: {
                const step = root.dragY < 32 ? -12 : 12;
                list.contentY = Math.max(0, Math.min(list.contentHeight - list.height,
                                                     list.contentY + step));
                root.followDrag();
            }
        }
        // Where a dragged row would land.
        Rectangle {
            z: 2
            visible: root.dropGap >= 0
            x: 8
            width: list.width - 16
            height: 2
            radius: 1
            color: ColorScope.scheme.primary
            y: {
                if (root.dropGap < 0)
                    return 0;
                const item = list.itemAtIndex(Math.min(root.dropGap, list.count - 1));
                if (!item)
                    return -10;
                return (root.dropGap < list.count ? item.y : item.y + item.height) - list.contentY - 1;
            }
        }

        HoverHandler {
            id: listHover
        }

        delegate: Item {
            id: row
            required property int index
            required property string key
            required property string title
            required property string artistText
            required property var artists
            required property string albumKey
            required property string albumTitle
            required property string coverKey
            required property bool voice
            required property bool loaded
            required property bool unavailable

            readonly property bool isPlaying: index === root.playing
            readonly property bool active: rowHover.hovered
                                           || root.dragRow === index
                                           || (list.activeFocus && InputMode.keyboard
                                               && list.currentIndex === index)

            width: ListView.view.width
            height: root.rowHeight
            opacity: root.dragRow === index ? 0.5 : 1

            // Over the whole row, the disabled item of a song that can't play included.
            HoverHandler {
                id: rowHover
            }
            Accessible.role: Accessible.ListItem
            Accessible.name: title + " " + artistText

            ListItem {
                id: item
                anchors.fill: parent
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                highlighted: row.isPlaying
                enabled: !row.unavailable
                leftPadding: 40
                text: row.loaded ? row.title : " "
                // A voice: its podcast.
                supportingText: !row.loaded ? " " : row.voice ? row.albumTitle : row.artistText
                outerRadius: Theme.shape.medium
                first: true
                last: true
                leading: Component {
                    CoverArt {
                        width: 40
                        height: 40
                        coverKey: row.coverKey
                    }
                }
                // Room on the right for the remove button (outside: a song that can't
                // play is a disabled item, and can still be removed).
                rightPadding: root.editable ? 56 : 16
                trailingGlyph: row.isPlaying ? Symbols.graphic_eq : ""
                onClicked: {
                    list.currentIndex = row.index;
                    if (!row.isPlaying)
                        App.player.playAt(row.index);
                }

                // The list is one Tab stop; ↑/↓ move this ring.
                FocusRing {
                    shown: list.activeFocus && InputMode.keyboard && list.currentIndex === row.index
                    offset: -2
                    containerRadius: Theme.shape.medium
                }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: eventPoint => {
                    list.currentIndex = row.index;
                    menu.row = row;
                    menu.popup(row, eventPoint.position.x, eventPoint.position.y);
                }
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: root.editable
                opacity: row.active ? 1 : 0
                focusPolicy: Qt.NoFocus
                glyph: Symbols.close
                text: qsTr("从队列中删除")
                onClicked: App.player.removeAt(row.index)
            }

            // The handle: the only place a drag starts.
            MouseArea {
                id: handleArea
                width: 40
                height: parent.height
                visible: root.editable
                hoverEnabled: true
                preventStealing: true
                cursorShape: root.dragRow >= 0 ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                onPressed: mouse => {
                    list.currentIndex = row.index;
                    root.dragRow = row.index;
                    root.dragY = mapToItem(list, mouse.x, mouse.y).y;
                    root.followDrag();
                }
                onPositionChanged: mouse => {
                    if (!pressed)
                        return;
                    root.dragY = mapToItem(list, mouse.x, mouse.y).y;
                    root.followDrag();
                }
                onReleased: root.drop()
                onCanceled: {
                    root.dragRow = -1;
                    root.dropGap = -1;
                }

                Icon {
                    anchors.centerIn: parent
                    glyph: Symbols.drag_indicator
                    size: 20
                    color: ColorScope.scheme.onSurfaceVariant
                    opacity: row.active ? 1 : 0
                }
            }
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10
                                          && event.modifiers & Qt.ShiftModifier)) {
            const item = list.currentItem;
            if (!item)
                return;
            menu.row = item;
            menu.popup(item, 24, item.height);
            event.accepted = true;
        }
    }

    Menu {
        id: menu
        // The row the menu is for (its model roles).
        property var row
        readonly property var song: row ?? ({})

        MenuItem {
            text: qsTr("播放")
            glyph: Symbols.play_arrow
            onTriggered: App.player.playAt(menu.song.index)
        }
        MenuItem {
            text: qsTr("从队列中删除")
            glyph: Symbols.close
            enabled: root.editable
            onTriggered: App.player.removeAt(menu.song.index)
        }
        Instantiator {
            model: menu.song.artists ?? []
            delegate: MenuItem {
                required property var modelData
                enabled: modelData.key !== ""
                glyph: Symbols.person
                text: (menu.song.artists?.length ?? 0) > 1
                      ? qsTr("查看歌手：%1").arg(modelData.name) : qsTr("查看歌手")
                onTriggered: root.artistRequested(modelData.key)
            }
            // After 播放 and 从队列中删除.
            onObjectAdded: (index, object) => menu.insertItem(index + 2, object as MenuItem)
            onObjectRemoved: (index, object) => menu.removeItem(object as MenuItem)
        }
        MenuItem {
            text: menu.song.voice ? qsTr("查看播客") : qsTr("查看专辑")
            glyph: menu.song.voice ? Symbols.podcasts : Symbols.album
            enabled: (menu.song.albumKey ?? "") !== ""
            onTriggered: menu.song.voice ? root.podcastRequested(menu.song.albumKey)
                                         : root.albumRequested(menu.song.albumKey)
        }
        MenuItem {
            text: qsTr("加入歌单")
            glyph: Symbols.playlist_add
            visible: !(menu.song.voice ?? false)
            height: visible ? implicitHeight : 0
            enabled: menu.song.loaded ?? false
            onTriggered: root.addToPlaylistRequested(menu.song.key, menu.song.title)
        }
    }
}
