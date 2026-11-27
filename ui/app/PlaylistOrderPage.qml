pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// "调整歌曲顺序" of the account's own playlist (docs/ui/pages/playlist.md §13). The
// order changes here only and is saved with one request. Rows move by their handle
// (the selection moves together), Alt+↑/↓, Alt+Home/End or the row menu; the quick
// sort menu orders the whole list. Leaving with changes asks first.
Item {
    id: page

    required property AppShell shell
    property alias playlistKey: orderModel.playlistKey
    readonly property bool hasChanges: orderModel.changed
    // Set once the user chose to leave (saved or discarded): AppShell.back() goes.
    property bool leaving: false

    // Rows: x and width inside the page, as on the playlist page.
    readonly property real rowX: Math.max(24, (width - 1200) / 2)
    readonly property real rowWidth: Math.min(width - 48, 1200)
    readonly property real rowPitch: 48 + list.spacing
    readonly property bool showAlbum: rowWidth >= 640

    // Selected keys (key -> true), replaced as a whole so bindings see each change.
    property var selectedKeys: ({})
    property int currentRow: -1
    property int anchorRow: -1
    property bool keyboardUsed: false

    // A drag by a row's handle: the rows moving (the grabbed row's selection), the
    // grabbed row, where the pointer is (in the list) and the gap it points at.
    property list<int> dragRows
    property int dragRow: -1
    property real dragY: 0
    property real dragStartY: 0
    property int dropGap: -1

    function leave(): void {
        if (orderModel.changed)
            discardDialog.open();
        else
            close();
    }
    function close(): void {
        leaving = true;
        shell.back();
    }

    function keyAt(row: int): string {
        return orderModel.songs.data(orderModel.songs.index(row, 0), SongListModel.KeyRole) ?? "";
    }
    function selectOnly(row: int): void {
        const keys = {};
        if (row >= 0)
            keys[keyAt(row)] = true;
        selectedKeys = keys;
        anchorRow = row;
    }
    function toggle(row: int): void {
        const keys = Object.assign({}, selectedKeys);
        const key = keyAt(row);
        if (keys[key])
            delete keys[key];
        else
            keys[key] = true;
        selectedKeys = keys;
        anchorRow = row;
    }
    function selectRange(from: int, to: int): void {
        const keys = {};
        for (let row = Math.max(0, Math.min(from, to)); row <= Math.max(from, to); ++row)
            keys[keyAt(row)] = true;
        selectedKeys = keys;
    }
    // The rows an action on `row` is for: the selection when the row is in it.
    function actionRows(row: int): list<int> {
        if (!selectedKeys[keyAt(row)])
            return [row];
        const rows = [];
        const keys = orderModel.songs.keys();
        for (let i = 0; i < keys.length; ++i) {
            if (selectedKeys[keys[i]])
                rows.push(i);
        }
        return rows;
    }
    // Moves `rows` into `gap`; the current row follows its song.
    function moveRows(rows: list<int>, gap: int): void {
        const currentKey = currentRow >= 0 ? keyAt(currentRow) : "";
        orderModel.moveRows(rows, gap);
        if (currentKey !== "") {
            currentRow = orderModel.songs.keys().indexOf(currentKey);
            list.positionViewAtIndex(currentRow, ListView.Contain);
        }
        anchorRow = -1;
    }
    // Alt+↑/↓: the rows one place up or down, as a block.
    function step(rows: list<int>, delta: int): void {
        const first = Math.min(...rows);
        const last = Math.max(...rows);
        if (delta < 0 && first > 0)
            moveRows(rows, first - 1);
        else if (delta > 0 && last < list.count - 1)
            moveRows(rows, last + 2);
    }
    function moveTo(row: int, modifiers: int): void {
        if (list.count === 0)
            return;
        row = Math.max(0, Math.min(list.count - 1, row));
        currentRow = row;
        list.positionViewAtIndex(row, ListView.Contain);
        if (modifiers & Qt.ShiftModifier) {
            if (anchorRow < 0)
                anchorRow = row;
            selectRange(anchorRow, row);
        } else if (!(modifiers & Qt.ControlModifier)) {
            selectOnly(row);
        }
    }

    function followDrag(): void {
        const y = dragY + list.contentY - list.originY;
        dropGap = Math.max(0, Math.min(list.count, Math.round(y / rowPitch)));
    }
    function drop(): void {
        // A copy: reading a list property gives the property itself, cleared below.
        const rows = Array.from(dragRows);
        const gap = dropGap;
        dragRow = -1;
        dropGap = -1;
        dragRows = [];
        if (rows.length > 0 && gap >= 0)
            moveRows(rows, gap);
    }

    PlaylistOrderModel {
        id: orderModel
        onNotice: message => page.shell.notify(message)
        onSaved: page.close()
    }

    PageState {
        id: pageState
        anchors.fill: parent
        model: orderModel
        emptyInContent: true
    }

    // Rows on screen get their details (large playlists).
    function loadDetails(): void {
        const first = Math.max(0, Math.floor((list.contentY - list.originY) / rowPitch));
        orderModel.ensureLoaded(first - 20, first + Math.ceil(list.height / rowPitch) + 20);
    }

    Shortcut {
        sequences: App.shortcuts.keys.saveOrder
        enabled: page.visible && orderModel.changed
        onActivated: orderModel.save()
    }

    Item {
        id: content
        anchors.fill: parent
        opacity: pageState.contentOpacity
        visible: opacity > 0

        // Title, quick sort, cancel and save; does not scroll.
        Item {
            id: header
            x: page.rowX
            width: page.rowWidth
            height: 104

            Column {
                y: 24
                width: parent.width - actions.width - 16
                spacing: 4
                Text {
                    width: parent.width
                    text: qsTr("调整歌曲顺序")
                    elide: Text.ElideRight
                    color: Theme.color.onSurface
                    font: Theme.type.headlineSmall.font
                    Accessible.role: Accessible.Heading
                }
                Text {
                    width: parent.width
                    text: orderModel.sortPending
                          ? qsTr("正在取回歌曲信息，还剩 %1 首…").arg(orderModel.waiting)
                          : qsTr("%1 · %2 首").arg(orderModel.title)
                                              .arg(App.format.count(orderModel.songs.count))
                    elide: Text.ElideRight
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.bodyMedium.font
                }
            }

            Row {
                id: actions
                anchors.right: parent.right
                y: 28
                spacing: 8

                Button {
                    id: sortButton
                    variant: Md.Tonal
                    enabled: orderModel.songs.count > 1 && !orderModel.sortPending
                    glyph: Symbols.sort
                    trailingGlyph: Symbols.expand_more
                    text: qsTr("快速排序")
                    onClicked: sortMenu.popup(sortButton, 0, sortButton.height + 4)

                    Menu {
                        id: sortMenu
                        MenuItem {
                            text: qsTr("按歌名")
                            onTriggered: orderModel.sortBy(PlaylistOrderModel.Title)
                        }
                        MenuItem {
                            text: qsTr("按歌手")
                            onTriggered: orderModel.sortBy(PlaylistOrderModel.Artist)
                        }
                        MenuItem {
                            text: qsTr("按专辑")
                            onTriggered: orderModel.sortBy(PlaylistOrderModel.Album)
                        }
                        MenuItem {
                            text: qsTr("按时长（短的在前）")
                            onTriggered: orderModel.sortBy(PlaylistOrderModel.Duration)
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("倒序")
                            onTriggered: orderModel.reverse()
                        }
                    }
                }
                Button {
                    variant: Md.Text
                    text: qsTr("取消")
                    onClicked: page.leave()
                }
                Button {
                    variant: Md.Filled
                    enabled: orderModel.changed && !orderModel.saving
                    text: orderModel.saving ? qsTr("正在保存…") : qsTr("保存")
                    onClicked: orderModel.save()
                }
            }
        }

        ListView {
            id: list

            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            model: orderModel.songs
            clip: true
            spacing: 2
            bottomMargin: 24 + PageInsets.bottom
            boundsBehavior: Flickable.DragAndOvershootBounds
            // Wheel and touch scroll; mouse drags are for the handles.
            acceptedButtons: Qt.NoButton
            keyNavigationEnabled: false
            highlightFollowsCurrentItem: false
            activeFocusOnTab: true
            focus: true
            currentIndex: -1
            Accessible.role: Accessible.List
            Accessible.name: qsTr("歌曲顺序")
            ScrollBar.vertical: ScrollBar {}

            onContentYChanged: page.loadDetails()
            onCountChanged: Qt.callLater(page.loadDetails)
            onCurrentIndexChanged: {
                if (currentIndex !== -1)
                    currentIndex = -1;
            }
            onActiveFocusChanged: {
                if (activeFocus && page.currentRow < 0 && count > 0)
                    page.currentRow = 0;
            }

            // Ctrl+↑/↓ move without selecting here, before the window's volume shortcuts;
            // Ctrl+A and Ctrl+Space are the list's too.
            Keys.onShortcutOverride: event => event.accepted =
                (event.modifiers & Qt.ControlModifier)
                && (event.key === Qt.Key_Up || event.key === Qt.Key_Down
                    || event.key === Qt.Key_A || event.key === Qt.Key_Space)
            Keys.onPressed: event => {
                page.keyboardUsed = true;
                const alt = event.modifiers & Qt.AltModifier;
                const rows = page.currentRow >= 0 ? page.actionRows(page.currentRow) : [];
                switch (event.key) {
                case Qt.Key_Up:
                case Qt.Key_Down:
                    {
                        const delta = event.key === Qt.Key_Up ? -1 : 1;
                        if (alt && rows.length > 0)
                            page.step(rows, delta);
                        else
                            page.moveTo(page.currentRow + delta, event.modifiers);
                        break;
                    }
                case Qt.Key_Home:
                    if (alt && rows.length > 0)
                        page.moveRows(rows, 0);
                    else
                        page.moveTo(0, event.modifiers);
                    break;
                case Qt.Key_End:
                    if (alt && rows.length > 0)
                        page.moveRows(rows, list.count);
                    else
                        page.moveTo(list.count - 1, event.modifiers);
                    break;
                case Qt.Key_A:
                    if (event.modifiers & Qt.ControlModifier)
                        page.selectRange(0, list.count - 1);
                    else
                        event.accepted = false;
                    break;
                case Qt.Key_Space:
                    if (event.modifiers & Qt.ControlModifier && page.currentRow >= 0)
                        page.toggle(page.currentRow);
                    else
                        event.accepted = false;
                    break;
                case Qt.Key_Menu:
                    {
                        const current = list.itemAtIndex(page.currentRow);
                        if (current)
                            rowMenu.openFor(page.currentRow, current, page.rowX + 80, current.height);
                        break;
                    }
                case Qt.Key_Escape:
                    if (Object.keys(page.selectedKeys).length > 0)
                        page.selectedKeys = {};
                    else
                        page.leave();
                    break;
                default:
                    event.accepted = false;
                }
            }

            // Where the dragged rows go.
            Rectangle {
                x: page.rowX
                y: page.dropGap * page.rowPitch - list.spacing / 2 - height / 2
                z: 2
                width: page.rowWidth
                height: 3
                radius: height / 2
                color: Theme.color.primary
                visible: page.dragRow >= 0 && page.dropGap >= 0
            }

            delegate: Item {
                id: row

                required property int index
                required property string key
                required property string title
                required property string secondaryTitle
                required property string artistText
                required property string albumTitle
                required property string coverKey
                required property string durationText
                required property bool loaded
                required property bool missing

                readonly property bool selected: page.selectedKeys[key] === true
                readonly property bool dragged: page.dragRow === index
                readonly property bool moving: page.dragRow >= 0 && page.dragRows.includes(index)
                readonly property bool showsFocus: list.activeFocus && page.keyboardUsed
                                                   && index === page.currentRow

                // ListView puts delegates at x 0: the row's body is inset inside.
                z: dragged ? 1 : 0
                width: list.width
                height: 48
                Accessible.role: Accessible.ListItem
                Accessible.name: loaded ? qsTr("%1，%2").arg(title).arg(artistText)
                               : missing ? qsTr("歌曲信息暂时无法获取") : qsTr("正在加载")
                Accessible.selected: selected

                Item {
                    id: body
                    x: page.rowX
                    width: page.rowWidth
                    height: parent.height
                    opacity: row.moving && !row.dragged ? 0.38 : 1
                    // The grabbed row floats with the pointer.
                    transform: Translate {
                        y: row.dragged ? page.dragY - page.dragStartY : 0
                    }

                    Rectangle {
                        id: container
                        readonly property real outer: Theme.shape.large
                        readonly property real inner: Theme.shape.extraSmall
                        anchors.fill: parent
                        color: row.dragged ? Theme.color.surfaceContainerHighest
                             : row.selected ? Theme.color.secondaryContainer
                             : Theme.color.surfaceContainer
                        topLeftRadius: row.index === 0 || row.dragged ? outer : inner
                        topRightRadius: topLeftRadius
                        bottomLeftRadius: row.index === list.count - 1 || row.dragged ? outer : inner
                        bottomRightRadius: bottomLeftRadius

                        StateLayer {
                            anchors.fill: parent
                            topLeftRadius: container.topLeftRadius
                            topRightRadius: container.topRightRadius
                            bottomLeftRadius: container.bottomLeftRadius
                            bottomRightRadius: container.bottomRightRadius
                            hovered: hover.hovered && page.dragRow < 0
                            pressed: tap.pressed
                        }
                        FocusRing {
                            shown: row.showsFocus
                            offset: -5
                            containerTopLeftRadius: container.topLeftRadius
                            containerTopRightRadius: container.topRightRadius
                            containerBottomLeftRadius: container.bottomLeftRadius
                            containerBottomRightRadius: container.bottomRightRadius
                        }
                    }

                    HoverHandler {
                        id: hover
                    }
                    TapHandler {
                        id: tap
                        onTapped: (eventPoint, button) => {
                            page.keyboardUsed = false;
                            page.currentRow = row.index;
                            list.forceActiveFocus(Qt.MouseFocusReason);
                            if (point.modifiers & Qt.ShiftModifier && page.anchorRow >= 0)
                                page.selectRange(page.anchorRow, row.index);
                            else if (point.modifiers & Qt.ControlModifier)
                                page.toggle(row.index);
                            else
                                page.selectOnly(row.index);
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: eventPoint => {
                            page.keyboardUsed = false;
                            list.forceActiveFocus(Qt.MouseFocusReason);
                            if (!row.selected)
                                page.selectOnly(row.index);
                            page.currentRow = row.index;
                            rowMenu.openFor(row.index, body, eventPoint.position.x,
                                            eventPoint.position.y);
                        }
                    }

                    // The handle: the only place a drag starts. The pointer's own
                    // position decides the gap, so the line sits where the pointer is.
                    Item {
                        width: 48
                        height: parent.height

                        Icon {
                            anchors.centerIn: parent
                            glyph: Symbols.drag_indicator
                            size: 20
                            color: Theme.color.onSurfaceVariant
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: page.dragRow >= 0 ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            preventStealing: true
                            onPressed: mouse => {
                                list.forceActiveFocus(Qt.MouseFocusReason);
                                page.keyboardUsed = false;
                                page.currentRow = row.index;
                                if (!row.selected)
                                    page.selectOnly(row.index);
                                page.dragRows = page.actionRows(row.index);
                                page.dragRow = row.index;
                                page.dragY = mapToItem(list, mouse.x, mouse.y).y;
                                page.dragStartY = page.dragY;
                                page.followDrag();
                            }
                            onPositionChanged: mouse => {
                                page.dragY = mapToItem(list, mouse.x, mouse.y).y;
                                page.followDrag();
                            }
                            onReleased: page.drop()
                            onCanceled: {
                                page.dragRow = -1;
                                page.dropGap = -1;
                                page.dragRows = [];
                            }
                        }
                    }

                    Text {
                        x: 48
                        width: 36
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        text: row.index + 1
                        color: Theme.color.onSurfaceVariant
                        font: Fonts.tabular(Theme.type.labelLarge.font)
                    }
                    CoverArt {
                        id: cover
                        x: 100
                        width: 32
                        height: 32
                        anchors.verticalCenter: parent.verticalCenter
                        coverKey: row.coverKey
                    }

                    // Title, artists, album, duration; placeholders while loading.
                    Item {
                        id: columns
                        readonly property real available: parent.width - x - 16 - 56 - 16
                        readonly property real albumWidth: page.showAlbum ? available * 0.28 : 0
                        readonly property real artistWidth: available * (page.showAlbum ? 0.28 : 0.4)
                        readonly property real titleWidth: available - artistWidth - albumWidth
                                                           - (page.showAlbum ? 32 : 16)
                        x: cover.x + cover.width + 12
                        width: parent.width - x
                        height: parent.height
                        opacity: row.missing ? Theme.state.disabledContent : 1

                        Text {
                            width: columns.titleWidth
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.loaded || row.missing
                            text: row.missing ? qsTr("歌曲信息暂时无法获取")
                                : row.secondaryTitle !== "" ? qsTr("%1（%2）").arg(row.title)
                                                                          .arg(row.secondaryTitle)
                                : row.title
                            elide: Text.ElideRight
                            color: row.selected ? Theme.color.onSecondaryContainer
                                                : Theme.color.onSurface
                            font: Theme.type.bodyLarge.font
                        }
                        Skeleton {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !row.loaded && !row.missing
                            width: Math.min(180, columns.titleWidth)
                            height: 14
                        }
                        Text {
                            x: columns.titleWidth + 16
                            width: columns.artistWidth
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.loaded
                            text: row.artistText
                            elide: Text.ElideRight
                            color: Theme.color.onSurfaceVariant
                            font: Theme.type.bodyMedium.font
                        }
                        Text {
                            x: columns.titleWidth + columns.artistWidth + 32
                            width: columns.albumWidth
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.loaded && page.showAlbum
                            text: row.albumTitle
                            elide: Text.ElideRight
                            color: Theme.color.onSurfaceVariant
                            font: Theme.type.bodyMedium.font
                        }
                        Text {
                            x: columns.width - 16 - width
                            width: 56
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.loaded
                            horizontalAlignment: Text.AlignRight
                            text: row.durationText
                            color: Theme.color.onSurfaceVariant
                            font: Fonts.tabular(Theme.type.labelLarge.font)
                        }
                    }
                }
            }
        }

        // Scrolls while a dragged row is held near the top or bottom edge.
        Timer {
            interval: 16
            repeat: true
            running: page.dragRow >= 0
            onTriggered: {
                const step = page.dragY < 32 ? -8 : page.dragY > list.height - 32 ? 8 : 0;
                if (step === 0)
                    return;
                const top = list.originY;
                const bottom = list.originY + list.contentHeight + list.bottomMargin - list.height;
                const before = list.contentY;
                list.contentY = Math.max(top, Math.min(bottom, list.contentY + step));
                // The grabbed row stays under the pointer while the list moves.
                page.dragStartY -= list.contentY - before;
                page.followDrag();
            }
        }
    }

    Menu {
        id: rowMenu

        property list<int> rows

        function openFor(row: int, anchor: Item, x: real, y: real): void {
            rows = page.actionRows(row);
            popup(anchor, x, y);
        }

        MenuItem {
            glyph: Symbols.vertical_align_top
            text: rowMenu.rows.length > 1 ? qsTr("把选中的 %1 首移到最前").arg(rowMenu.rows.length)
                                          : qsTr("移到最前")
            trailingText: "Alt+Home"
            onTriggered: page.moveRows(rowMenu.rows, 0)
        }
        MenuItem {
            glyph: Symbols.vertical_align_bottom
            text: rowMenu.rows.length > 1 ? qsTr("把选中的 %1 首移到最后").arg(rowMenu.rows.length)
                                          : qsTr("移到最后")
            trailingText: "Alt+End"
            onTriggered: page.moveRows(rowMenu.rows, list.count)
        }
    }

    Dialog {
        id: discardDialog
        title: qsTr("放弃对歌曲顺序的修改？")
        supportingText: qsTr("修改还没有保存，离开后会丢失。")
        acceptText: qsTr("放弃")
        rejectText: qsTr("继续调整")
        onAccepted: page.close()
    }
}
