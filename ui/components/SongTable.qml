pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Songs as a desktop table (docs/ui/pages/playlist.md §3–§5): number, cover with
// title and artists, album, liked, duration. The column titles sort (`sortable`).
// Albums leave out the cover and album columns and number by track; daily songs add
// a reason column (docs/ui/pages/album-daily.md). `headerContent`
// scrolls above the column titles, which then stay pinned `pinnedTop` below the top.
// Mouse: a click selects (Ctrl toggles, Shift extends), a double click or the play
// button that replaces the number on hover plays the shown order from that row.
// Keyboard: the table is one Tab stop; arrows, PageUp/PageDown and Home/End move
// the current row (Shift extends the selection, Ctrl moves without selecting),
// Enter plays it, Ctrl+A selects all, the menu key or Shift+F10 opens its menu.
// Typed characters go to `typed` (the page's in-page find). `removable` adds
// removing (menu, Delete) through `removeRequested`. Reordering has its own page
// (playlist.md §13), away from accidental drags. `voices` shows podcast voices
// (search.md §3.4): episode numbers, the podcast under the title, no liking or
// adding to playlists; they play through App.player.playVoices.
//   SongTable { songs: SortedSongsModel { source: pageModel.songs } }
Item {
    id: table

    required property SortedSongsModel songs
    property Component headerContent
    // Under the rows (e.g. the next page loading).
    property Component footerContent
    // Off when the page shows something else than songs (artist.md §2): no column
    // titles, here or pinned.
    property bool showColumnTitles: true
    property real pinnedTop: 0
    // Where the pinned titles' backing starts, from the table's left edge: past a pane
    // the table reaches under (SongCollectionPage), so it does not cover the pane's edge.
    property real pinnedLeft: 0
    property real maxContentWidth: 1200
    property bool showAlbumAction: true
    property bool showLiked: true
    // Whether the column titles sort (only playlists).
    property bool sortable: true
    property bool showCover: true
    // Whether there is an album column (it still hides in narrow windows).
    property bool albumColumn: true
    // A recommendation reason column, between title and album; hides with the album's.
    property bool reasonColumn: false
    property string reasonTitle: qsTr("推荐理由")
    // Numbers are the place in the list (an album's track numbers), not the shown row:
    // finding keeps them.
    property bool numberByPosition: false
    // The page's own playlist: songs can be removed.
    property bool removable: false
    // Podcast voices instead of songs; pages also turn off the liked and album columns.
    property bool voices: false
    // Numbers are ranks: the first three in primary (the listening ranking).
    property bool ranked: false
    // Where these songs come from (Playback.Origin, its id and name), for the listening
    // reports and the local statistics.
    property int origin: Playback.NoOrigin
    property string originId
    property string originName
    // Voices can't play until the backend can (search.md §3.4).
    readonly property bool playable: !voices || App.player.canPlayVoices
    readonly property ListView view: list
    readonly property real rowPitch: 56 + list.spacing
    // The rows on screen (rows start at content y 0, under the header), for loading
    // their details.
    readonly property int firstVisibleRow: Math.max(0, Math.floor(list.contentY / rowPitch))
    readonly property int lastVisibleRow: Math.max(0, Math.floor((list.contentY + list.height)
                                                                 / rowPitch))
    // Bars pinned over the top of the table leave the scroll bar this much room.
    readonly property real scrollBarWidth: 12
    // How far the content has scrolled from its top.
    readonly property real scrolled: list.contentY - list.originY

    // Rows: x and width inside the panel; columns: x and width inside a row.
    readonly property real rowX: Math.max(24, (width - maxContentWidth) / 2)
    readonly property real rowWidth: Math.min(width - 48, maxContentWidth)
    readonly property bool showAlbum: albumColumn && rowWidth >= 640
    readonly property bool showReason: reasonColumn && showAlbum
    readonly property real numberWidth: 40
    readonly property real titleX: 16 + numberWidth + 16
    readonly property real durationWidth: 56
    readonly property real moreWidth: 40
    readonly property real likedWidth: showLiked ? 40 : 0
    readonly property real flexWidth: rowWidth - titleX - 16 - durationWidth - moreWidth - 16
                                      - (showAlbum ? 16 : 0) - (showReason ? 16 : 0)
                                      - (showLiked ? likedWidth + 16 : 0)
    readonly property real reasonWidth: showReason ? Math.round(flexWidth * 0.28) : 0
    readonly property real albumWidth: showAlbum ? Math.round(flexWidth * (showReason ? 0.28 : 0.4))
                                                 : 0
    readonly property real titleWidth: flexWidth - albumWidth - reasonWidth
    // Title and artists: after the cover when there is one.
    readonly property real titleTextX: titleX + (showCover ? 52 : 0)
    readonly property real titleTextWidth: titleWidth - (showCover ? 52 : 0)
    readonly property real reasonX: titleX + titleWidth + 16
    readonly property real albumX: reasonX + (showReason ? reasonWidth + 16 : 0)
    readonly property real durationX: rowWidth - 16 - moreWidth - durationWidth
    readonly property real likedX: durationX - 16 - likedWidth

    // Selected keys (key -> true), replaced as a whole so bindings see each change.
    property var selectedKeys: ({})
    readonly property int selectionCount: Object.keys(selectedKeys).length
    // Where Shift+click and Shift+arrows extend the selection from.
    property int anchorRow: -1
    // The keyboard's row. Not ListView.currentIndex: the list would move the focus
    // to the current delegate, taking it from the header's controls (the find field).
    property int currentRow: -1
    // The current row shows its focus ring only after keyboard use.
    property bool keyboardUsed: false
    // Typed text goes on in a find (typed()): single-key shortcuts wait while the table
    // has the focus.
    property bool typingFinds: false

    signal albumRequested(string key)
    signal podcastRequested(string key)
    signal artistRequested(string key)
    // "歌曲详情" for one song (not voices): the song's own page.
    signal songRequested(string key)
    // "查看评论" for one song (not voices).
    signal commentsRequested(string key, string title, string artistText, string coverKey)
    signal typed(string text)
    signal signInRequested
    signal addToPlaylistRequested(list<string> keys)
    signal removeRequested(list<string> keys)



    // The songs an action on `row` is for: the selection when the row is in it.
    function actionKeys(row: int): list<string> {
        const key = songs.keyAt(row);
        return selectedKeys[key] ? selectedInOrder() : [key];
    }
    function setLiked(key: string, liked: bool): void {
        if (!App.session.signedIn)
            signInRequested();
        else
            App.library.setLiked(key, liked);
    }

    // Plays the shown order from `row`, leaving out songs the service has no details for.
    function play(row: int): void {
        if (row < 0 || row >= songs.count || isMissing(row) || !playable)
            return;
        const key = songs.keyAt(row);
        const keys = songs.keys().filter((k, i) => !isMissing(i));
        if (voices)
            App.player.playVoices(keys, keys.indexOf(key));
        else
            App.player.playSongs(keys, keys.indexOf(key), table.origin, table.originId,
                                 table.originName);
    }
    function isMissing(row: int): bool {
        return songs.data(songs.index(row, 0), SongListModel.MissingRole) === true;
    }
    function selectOnly(row: int): void {
        const keys = {};
        if (row >= 0)
            keys[songs.keyAt(row)] = true;
        selectedKeys = keys;
        anchorRow = row;
    }
    function toggle(row: int): void {
        const keys = Object.assign({}, selectedKeys);
        const key = songs.keyAt(row);
        if (keys[key])
            delete keys[key];
        else
            keys[key] = true;
        selectedKeys = keys;
        anchorRow = row;
    }
    function selectRange(from: int, to: int): void {
        const keys = {};
        const first = Math.max(0, Math.min(from, to));
        for (let row = first; row <= Math.max(from, to); ++row)
            keys[songs.keyAt(row)] = true;
        selectedKeys = keys;
    }
    function selectAll(): void {
        const keys = {};
        for (const key of songs.keys())
            keys[key] = true;
        selectedKeys = keys;
    }
    // Selected keys in the shown order.
    function selectedInOrder(): list<string> {
        return songs.keys().filter(key => table.selectedKeys[key] === true);
    }
    // Focus on the table itself. The list is a focus scope that hands the focus back
    // to the header control it last held (the find field), so that one lets go first.
    function takeFocus(reason: int): void {
        list.forceActiveFocus(reason);
        // The header's nested scopes (Loader) each hold on to their child.
        for (let i = 0; i < 8; ++i) {
            const held = list.Window.activeFocusItem;
            if (!held || held === list)
                break;
            held.focus = false;
            list.forceActiveFocus(reason);
        }
    }
    function focusTable(): void {
        if (currentRow < 0 && list.count > 0)
            currentRow = 0;
        takeFocus(Qt.TabFocusReason);
    }

    // Keyboard move: Shift extends from the anchor, Ctrl only moves.
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
    function openMenu(row: int, anchor: Item, x: real, y: real): void {
        if (!selectedKeys[songs.keyAt(row)])
            selectOnly(row);
        currentRow = row;
        songMenu.openFor(row, anchor, x, y);
    }

    // Selected rows that left the list (reloaded, filtered out) are no longer selected.
    Connections {
        target: table.songs
        function onModelReset() {
            table.selectedKeys = {};
            table.anchorRow = -1;
        }
        // Removed songs leave the selection.
        function onRowsRemoved() {
            const keys = {};
            for (const key of table.songs.keys()) {
                if (table.selectedKeys[key])
                    keys[key] = true;
            }
            table.selectedKeys = keys;
            table.currentRow = Math.min(table.currentRow, table.songs.count - 1);
            table.anchorRow = -1;
        }
        function onFilterTextChanged() {
            table.selectedKeys = {};
            table.anchorRow = -1;
            table.currentRow = table.songs.count > 0 ? 0 : -1;
        }
    }

    // A sortable column title.
    component SortTitle: T.AbstractButton {
        id: sortTitle

        required property int field
        property bool alignRight: false
        // The pinned copy is not a Tab stop (the header's titles are).
        property bool pinned: false
        readonly property bool active: table.songs.sortField === field

        height: 40
        enabled: table.sortable
        focusPolicy: pinned || !table.sortable ? Qt.NoFocus : Qt.TabFocus
        Accessible.name: text
        onClicked: table.songs.cycleSort(field)

        contentItem: Item {
            Row {
                x: sortTitle.alignRight ? parent.width - width : 0
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                layoutDirection: sortTitle.alignRight ? Qt.RightToLeft : Qt.LeftToRight
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: sortTitle.text
                    color: sortTitle.active || sortTitle.hovered ? ColorScope.scheme.onSurface
                                                                : ColorScope.scheme.onSurfaceVariant
                    font: Theme.type.labelMedium.font
                }
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: sortTitle.active
                    glyph: table.songs.descending ? Symbols.arrow_downward : Symbols.arrow_upward
                    size: 16
                    color: ColorScope.scheme.onSurface
                }
            }
        }
        background: FocusRing {
            shown: sortTitle.visualFocus
            containerRadius: Theme.shape.small
            offset: -2
        }
    }



    // The column titles, in the header and pinned while the header is scrolled away.
    component ColumnTitles: Item {
        id: titles

        property bool pinned: false

        width: table.rowWidth
        height: 40

        Text {
            x: 16
            width: table.numberWidth
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: "#"
            color: ColorScope.scheme.onSurfaceVariant
            font: Theme.type.labelMedium.font
        }
        SortTitle {
            x: table.titleX
            width: table.titleWidth
            pinned: titles.pinned
            field: SortedSongsModel.Title
            text: qsTr("标题")
        }
        Text {
            x: table.reasonX
            width: table.reasonWidth
            anchors.verticalCenter: parent.verticalCenter
            visible: table.showReason
            text: table.reasonTitle
            color: ColorScope.scheme.onSurfaceVariant
            font: Theme.type.labelMedium.font
        }
        SortTitle {
            x: table.albumX
            width: table.albumWidth
            visible: table.showAlbum
            pinned: titles.pinned
            field: SortedSongsModel.Album
            text: qsTr("专辑")
        }
        Text {
            x: table.likedX
            width: table.likedWidth
            anchors.verticalCenter: parent.verticalCenter
            visible: table.showLiked
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("喜欢")
            color: ColorScope.scheme.onSurfaceVariant
            font: Theme.type.labelMedium.font
        }
        SortTitle {
            x: table.durationX
            width: table.durationWidth
            alignRight: true
            pinned: titles.pinned
            field: SortedSongsModel.Duration
            text: qsTr("时长")
        }
    }

    // A name that opens something: underlined while hovered.
    component LinkText: Text {
        id: link

        signal activated

        elide: Text.ElideRight
        font.underline: linkHover.hovered
        HoverHandler {
            id: linkHover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: link.activated()
        }
    }

    ListView {
        id: list

        anchors.fill: parent
        model: table.songs
        clip: true
        spacing: 2
        bottomMargin: 24 + PageInsets.bottom
        // Overshoots and springs back at the ends.
        boundsBehavior: Flickable.DragAndOvershootBounds
        // Wheel and touch scroll; mouse drags don't (desktop).
        acceptedButtons: Qt.NoButton
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        activeFocusOnTab: true
        Accessible.role: Accessible.Table
        ScrollBar.vertical: ScrollBar {}

        // At the top until the user scrolls: a header that grows once its data arrives
        // moves originY, and the view would otherwise keep showing the old top.
        property bool pinnedToTop: true
        onContentYChanged: pinnedToTop = contentY <= originY + 1
        onOriginYChanged: {
            if (pinnedToTop)
                contentY = originY;
        }

        // Kept at -1 (see currentRow); the list sets 0 itself when rows arrive.
        currentIndex: -1
        onCurrentIndexChanged: {
            if (currentIndex !== -1)
                currentIndex = -1;
        }
        onActiveFocusChanged: {
            if (activeFocus && table.currentRow < 0 && count > 0)
                table.currentRow = 0;
        }

        header: Column {
            width: list.width
            Loader {
                width: parent.width
                sourceComponent: table.headerContent
            }
            ColumnTitles {
                x: table.rowX
                visible: table.showColumnTitles
            }
        }
        footer: Loader {
            width: list.width
            sourceComponent: table.footerContent
        }

        // Ctrl+↑/↓ move without selecting here, before the window's volume shortcuts
        // (Ctrl+A and Ctrl+Space are the table's too); typed text goes to the find before
        // single-key shortcuts.
        Keys.onShortcutOverride: event => {
            const command = event.modifiers & (Qt.ControlModifier | Qt.AltModifier
                                               | Qt.MetaModifier);
            const own = (event.modifiers & Qt.ControlModifier)
                && (event.key === Qt.Key_Up || event.key === Qt.Key_Down
                    || event.key === Qt.Key_A || event.key === Qt.Key_Space);
            event.accepted = list.Window.activeFocusItem === list
                && (own || table.typingFinds && !command && event.text.trim().length > 0);
        }
        Keys.onPressed: event => {
            // Keys from the header's controls (find field, sort titles) bubble up here.
            if (list.Window.activeFocusItem !== list) {
                event.accepted = false;
                return;
            }
            table.keyboardUsed = true;
            const rows = Math.max(1, Math.floor(list.height / 58) - 1);
            const ctrl = event.modifiers & Qt.ControlModifier;
            switch (event.key) {
            case Qt.Key_Up:
                table.moveTo(table.currentRow - 1, event.modifiers);
                break;
            case Qt.Key_Down:
                table.moveTo(table.currentRow + 1, event.modifiers);
                break;
            case Qt.Key_Delete:
                if (table.removable && table.currentRow >= 0)
                    table.removeRequested(table.actionKeys(table.currentRow));
                else
                    event.accepted = false;
                break;
            case Qt.Key_PageUp:
                table.moveTo(table.currentRow - rows, event.modifiers);
                break;
            case Qt.Key_PageDown:
                table.moveTo(table.currentRow + rows, event.modifiers);
                break;
            case Qt.Key_Home:
                table.moveTo(0, event.modifiers);
                list.positionViewAtBeginning();
                break;
            case Qt.Key_End:
                table.moveTo(list.count - 1, event.modifiers);
                break;
            case Qt.Key_Return:
            case Qt.Key_Enter:
                table.play(table.currentRow);
                break;
            case Qt.Key_Space:
                if (ctrl && table.currentRow >= 0)
                    table.toggle(table.currentRow);
                else
                    event.accepted = false;
                break;
            case Qt.Key_A:
                if (ctrl)
                    table.selectAll();
                else
                    event.accepted = false;
                break;
            case Qt.Key_Menu:
            case Qt.Key_F10:
                if (table.currentRow >= 0 && (event.key === Qt.Key_Menu
                                              || event.modifiers & Qt.ShiftModifier)) {
                    const current = list.itemAtIndex(table.currentRow);
                    if (current)
                        table.openMenu(table.currentRow, current, table.rowX + 48,
                                       current.height);
                } else {
                    event.accepted = false;
                }
                break;
            case Qt.Key_Escape:
                if (table.selectionCount > 0)
                    table.selectedKeys = {};
                else
                    event.accepted = false;
                break;
            default:
                // Printable text without shortcuts goes to the in-page find.
                if (event.text.length > 0 && event.text.trim().length > 0
                        && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier
                                                | Qt.MetaModifier))) {
                    table.typed(event.text);
                } else {
                    event.accepted = false;
                }
            }
        }

        delegate: Item {
            id: row

            required property int index
            required property string key
            required property string title
            required property string secondaryTitle
            required property var artists
            required property string artistText
            required property string albumKey
            required property string albumTitle
            required property string coverKey
            required property string durationText
            required property bool loaded
            required property bool unavailable
            required property bool current
            required property bool liked
            required property bool missing
            required property int position
            required property int serial
            required property string reason

            readonly property bool selected: table.selectedKeys[key] === true
            readonly property bool hovered: hover.hovered
            readonly property bool playing: current && App.player.status === Playback.Playing
            readonly property bool first: index === 0
            readonly property bool last: index === list.count - 1
            readonly property bool showsFocus: list.activeFocus && table.keyboardUsed
                                               && index === table.currentRow
            readonly property color strongColor: current ? ColorScope.scheme.primary
                                               : Theme.mix(ColorScope.scheme.onSurface,
                                                           ColorScope.scheme.onSecondaryContainer,
                                                           selection.value)
            readonly property color mutedColor: Theme.mix(ColorScope.scheme.onSurfaceVariant,
                                                          ColorScope.scheme.onSecondaryContainer,
                                                          selection.value)

            // ListView puts delegates at x 0: the row's body is inset inside.
            width: list.width
            height: 56
            Accessible.role: Accessible.Row
            Accessible.name: loaded ? qsTr("%1，%2").arg(title).arg(artistText)
                           : missing ? qsTr("歌曲信息暂时无法获取") : qsTr("正在加载")
            Accessible.selected: selected

            Item {
                id: rowBody
                x: table.rowX
                width: table.rowWidth
                height: parent.height

                // Selected rows and the playing one are SecondaryContainer; the playing
                // one is rounder (redesign-md3e.md §7.3).
                Spring {
                    id: selection
                    target: row.selected || row.current ? 1 : 0
                    spec: Theme.motion.defaultEffects
                }
                Spring {
                    id: corner
                    target: row.current ? Theme.shape.largeIncreased
                          : row.selected ? Theme.shape.large
                          : row.hovered || row.showsFocus ? Theme.shape.medium
                          : Theme.shape.extraSmall
                    spec: Theme.motion.fastSpatial
                    threshold: 0.1
                }

                Rectangle {
                    id: container
                    readonly property real inner: Math.max(0, corner.value)
                    readonly property real topRadius: row.first ? Math.max(Theme.shape.large, inner)
                                                                : inner
                    readonly property real bottomRadius: row.last ? Math.max(Theme.shape.large, inner)
                                                                  : inner
                    anchors.fill: parent
                    color: Theme.mix(ColorScope.scheme.surfaceContainer, ColorScope.scheme.secondaryContainer,
                                     selection.value)
                    topLeftRadius: topRadius
                    topRightRadius: topRadius
                    bottomLeftRadius: bottomRadius
                    bottomRightRadius: bottomRadius

                    StateLayer {
                        anchors.fill: parent
                        topLeftRadius: container.topRadius
                        topRightRadius: container.topRadius
                        bottomLeftRadius: container.bottomRadius
                        bottomRightRadius: container.bottomRadius
                        hovered: row.hovered
                        pressed: tap.pressed
                        pressX: tap.point.position.x
                        pressY: tap.point.position.y
                    }
                    FocusRing {
                        shown: row.showsFocus
                        offset: -5
                        containerTopLeftRadius: container.topRadius
                        containerTopRightRadius: container.topRadius
                        containerBottomLeftRadius: container.bottomRadius
                        containerBottomRightRadius: container.bottomRadius
                    }
                }

                HoverHandler {
                    id: hover
                }
                TapHandler {
                    id: tap
                    onTapped: (eventPoint, button) => {
                        table.keyboardUsed = false;
                        table.currentRow = row.index;
                        table.takeFocus(Qt.MouseFocusReason);
                        if (point.modifiers & Qt.ShiftModifier && table.anchorRow >= 0)
                            table.selectRange(table.anchorRow, row.index);
                        else if (point.modifiers & Qt.ControlModifier)
                            table.toggle(row.index);
                        else
                            table.selectOnly(row.index);
                    }
                    onDoubleTapped: table.play(row.index)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        table.keyboardUsed = false;
                        table.takeFocus(Qt.MouseFocusReason);
                        table.openMenu(row.index, rowBody, eventPoint.position.x,
                                       eventPoint.position.y);
                    }
                }

                Item {
                    anchors.fill: parent
                    opacity: row.unavailable || row.missing ? Theme.state.disabledContent : 1

                    // Number; the play (or pause) button on hover; the equalizer while
                    // this song plays.
                    Item {
                        x: 16
                        width: table.numberWidth
                        height: parent.height
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !row.hovered && !row.current
                            text: table.voices && row.serial >= 0 ? row.serial
                                : table.numberByPosition ? row.position : row.index + 1
                            color: table.ranked && row.index < 3 ? ColorScope.scheme.primary
                                                                 : row.mutedColor
                            font: Fonts.tabular(Theme.type.labelLarge.font)
                        }
                        Icon {
                            anchors.right: parent.right
                            anchors.rightMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !row.hovered && row.current
                            glyph: Symbols.graphic_eq
                            size: 20
                            color: ColorScope.scheme.primary
                        }
                        // The row's buttons exist only while they show: a page has dozens
                        // of rows, and each button is a few dozen objects.
                        Loader {
                            anchors.right: parent.right
                            anchors.rightMargin: -6
                            anchors.verticalCenter: parent.verticalCenter
                            active: row.hovered && row.loaded
                            sourceComponent: IconButton {
                                enabled: table.playable
                                size: Md.XSmall
                                focusPolicy: Qt.NoFocus
                                showToolTip: false
                                glyph: row.playing ? Symbols.pause : Symbols.play_arrow
                                iconFill: 1
                                text: row.playing ? qsTr("暂停") : qsTr("播放")
                                onClicked: {
                                    if (row.current)
                                        App.player.playPause();
                                    else
                                        table.play(row.index);
                                }
                            }
                        }
                    }

                    CoverArt {
                        x: table.titleX
                        visible: table.showCover
                        width: 40
                        height: 40
                        anchors.verticalCenter: parent.verticalCenter
                        coverKey: row.coverKey
                    }

                    Column {
                        x: table.titleTextX
                        width: table.titleTextWidth
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.loaded
                        spacing: 2

                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            textFormat: Text.StyledText
                            text: {
                                const escape = s => s.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                                                     .replace(/>/g, "&gt;");
                                const alias = row.secondaryTitle === "" ? ""
                                            : " <font color=\"" + row.mutedColor + "\">"
                                              + escape(qsTr("（%1）").arg(row.secondaryTitle))
                                              + "</font>";
                                return escape(row.title) + alias;
                            }
                            color: row.strongColor
                            font: Theme.type.bodyLarge.font
                        }
                        // A voice's podcast opens its page.
                        LinkText {
                            width: Math.min(implicitWidth, parent.width)
                            visible: table.voices
                            enabled: row.albumKey !== ""
                            text: row.albumTitle
                            color: row.mutedColor
                            font: Theme.type.bodyMedium.font
                            onActivated: table.podcastRequested(row.albumKey)
                        }
                        // Each artist opens their page.
                        Row {
                            width: parent.width
                            visible: !table.voices
                            clip: true
                            Repeater {
                                model: row.artists
                                delegate: Row {
                                    id: artist
                                    required property int index
                                    required property var modelData
                                    LinkText {
                                        text: artist.modelData.name
                                        color: row.mutedColor
                                        font: Theme.type.bodyMedium.font
                                        enabled: artist.modelData.key !== ""
                                        onActivated: table.artistRequested(artist.modelData.key)
                                    }
                                    Text {
                                        visible: artist.index < row.artists.length - 1
                                        text: qsTr(" / ")
                                        color: row.mutedColor
                                        font: Theme.type.bodyMedium.font
                                    }
                                }
                            }
                        }
                    }

                    // The service has no details for this song.
                    Text {
                        x: table.titleTextX
                        width: table.titleTextWidth
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.missing
                        text: qsTr("歌曲信息暂时无法获取")
                        elide: Text.ElideRight
                        color: ColorScope.scheme.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }

                    // Details still on their way: placeholder bars.
                    Column {
                        x: table.titleTextX
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !row.loaded && !row.missing
                        spacing: 8
                        Skeleton {
                            width: Math.min(200, table.titleTextWidth)
                            height: 14
                        }
                        Skeleton {
                            width: Math.min(120, table.titleTextWidth)
                            height: 12
                        }
                    }

                    Text {
                        x: table.reasonX
                        width: table.reasonWidth
                        anchors.verticalCenter: parent.verticalCenter
                        visible: table.showReason && row.loaded
                        text: row.reason
                        elide: Text.ElideRight
                        color: row.mutedColor
                        font: Theme.type.bodyMedium.font
                    }

                    LinkText {
                        x: table.albumX
                        width: Math.min(implicitWidth, table.albumWidth)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: table.showAlbum && row.loaded
                        enabled: row.albumKey !== "" && table.showAlbumAction
                        text: row.albumTitle
                        color: row.mutedColor
                        font: Theme.type.bodyMedium.font
                        onActivated: table.albumRequested(row.albumKey)
                    }

                    // The like button while hovered; otherwise its icon alone, drawn as the
                    // idle button draws it (a standard XSmall icon button).
                    Item {
                        x: table.likedX + (table.likedWidth - width) / 2
                        width: 32
                        height: 32
                        anchors.verticalCenter: parent.verticalCenter
                        visible: table.showLiked && row.loaded

                        Icon {
                            anchors.centerIn: parent
                            visible: !likeButton.active
                            glyph: Symbols.favorite
                            size: 20
                            fill: row.liked ? 1 : 0
                            color: row.liked ? ColorScope.scheme.primary
                                             : ColorScope.scheme.onSurfaceVariant
                        }
                        Loader {
                            id: likeButton
                            anchors.centerIn: parent
                            active: row.hovered
                            sourceComponent: IconButton {
                                size: Md.XSmall
                                focusPolicy: Qt.NoFocus
                                checkable: true
                                checked: row.liked
                                iconFill: row.liked ? 1 : 0
                                pops: true
                                glyph: Symbols.favorite
                                text: row.liked ? qsTr("取消喜欢") : qsTr("喜欢")
                                onToggled: {
                                    table.setLiked(row.key, checked);
                                    checked = Qt.binding(() => row.liked);
                                }
                            }
                        }
                    }

                    Text {
                        x: table.durationX
                        width: table.durationWidth
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.loaded
                        horizontalAlignment: Text.AlignRight
                        text: row.durationText
                        color: row.mutedColor
                        font: Fonts.tabular(Theme.type.labelLarge.font)
                    }
                }

                Loader {
                    x: parent.width - 16 - table.moreWidth + (table.moreWidth - width) / 2
                    anchors.verticalCenter: parent.verticalCenter
                    active: row.hovered || row.showsFocus || songMenu.row === row.index
                    sourceComponent: IconButton {
                        id: more
                        size: Md.XSmall
                        focusPolicy: Qt.NoFocus
                        enabled: row.loaded
                        glyph: Symbols.more_vert
                        text: qsTr("更多")
                        onClicked: table.openMenu(row.index, more, 0, more.height)
                    }
                }
            }
        }
    }

    ColumnTitles {
        x: table.rowX
        y: table.pinnedTop
        pinned: true
        // Once the header's own titles scroll under the pinned top.
        visible: table.showColumnTitles && list.headerItem !== null
                 && list.headerItem.y + list.headerItem.height - 40 - list.contentY < table.pinnedTop

        // Covers the rows scrolling under it, for the pointer too; not the scroll bar.
        Rectangle {
            z: -1
            x: table.pinnedLeft - table.rowX
            width: table.width - table.scrollBarWidth - table.pinnedLeft
            height: parent.height
            color: ColorScope.scheme.surface
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.AllButtons
            }
            HoverHandler {
                blocking: true
            }
        }
    }

    Menu {
        id: songMenu

        // The row the menu is open for; -1 when closed.
        property int row: -1
        property string key
        property string albumKey
        property var artists: []
        property bool liked: false
        // The songs the actions are for (the selection when the row is in it).
        property list<string> keys
        readonly property int songCount: keys.length

        function openFor(target: int, anchor: Item, x: real, y: real): void {
            row = target;
            const index = table.songs.index(target, 0);
            key = table.songs.keyAt(target);
            keys = table.actionKeys(target);
            albumKey = table.songs.data(index, SongListModel.AlbumKeyRole) ?? "";
            artists = table.songs.data(index, SongListModel.ArtistsRole) ?? [];
            liked = table.songs.data(index, SongListModel.LikedRole) ?? false;
            popup(anchor, x, y);
        }

        onClosed: row = -1

        MenuItem {
            glyph: Symbols.play_arrow
            enabled: table.playable
            text: !table.playable ? qsTr("声音播放即将支持")
                : songMenu.songCount > 1 ? qsTr("播放选中的 %1 首").arg(songMenu.songCount)
                : qsTr("播放")
            onTriggered: {
                if (songMenu.songCount <= 1)
                    table.play(songMenu.row);
                else if (table.voices)
                    App.player.playVoices(songMenu.keys, 0);
                else
                    App.player.playSongs(songMenu.keys, 0, table.origin, table.originId,
                                         table.originName);
            }
        }
        MenuItem {
            visible: !table.voices
            height: visible ? implicitHeight : 0
            glyph: Symbols.queue_play_next
            text: qsTr("下一首播放")
            onTriggered: App.player.addSongs(songMenu.keys, true, table.origin, table.originId,
                                             table.originName)
        }
        MenuItem {
            visible: !table.voices
            height: visible ? implicitHeight : 0
            glyph: Symbols.add_to_queue
            text: qsTr("加到播放队列")
            onTriggered: App.player.addSongs(songMenu.keys, false, table.origin, table.originId,
                                             table.originName)
        }
        MenuSeparator {
            visible: !table.voices
        }
        MenuItem {
            visible: songMenu.songCount === 1 && !table.voices
            height: visible ? implicitHeight : 0
            glyph: Symbols.favorite
            text: songMenu.liked ? qsTr("取消喜欢") : qsTr("喜欢")
            onTriggered: table.setLiked(songMenu.key, !songMenu.liked)
        }
        MenuItem {
            visible: !table.voices
            height: visible ? implicitHeight : 0
            glyph: Symbols.playlist_add
            text: songMenu.songCount > 1 ? qsTr("收藏选中的 %1 首到歌单…").arg(songMenu.songCount)
                                         : qsTr("收藏到歌单…")
            onTriggered: table.addToPlaylistRequested(songMenu.keys)
        }
        MenuItem {
            visible: songMenu.songCount === 1 && !table.voices
            height: visible ? implicitHeight : 0
            glyph: Symbols.info
            text: qsTr("歌曲详情")
            onTriggered: table.songRequested(songMenu.key)
        }
        MenuItem {
            visible: songMenu.songCount === 1 && !table.voices
            height: visible ? implicitHeight : 0
            glyph: Symbols.comment
            text: qsTr("查看评论")
            onTriggered: {
                const index = table.songs.index(songMenu.row, 0);
                table.commentsRequested(songMenu.key,
                                        table.songs.data(index, SongListModel.TitleRole) ?? "",
                                        table.songs.data(index, SongListModel.ArtistTextRole) ?? "",
                                        table.songs.data(index, SongListModel.CoverKeyRole) ?? "");
            }
        }
        MenuItem {
            visible: songMenu.songCount === 1
            height: visible ? implicitHeight : 0
            glyph: Symbols.link
            text: qsTr("复制链接")
            onTriggered: {
                // A voice's author is its podcast.
                const index = table.songs.index(songMenu.row, 0);
                App.share.copy(table.voices ? Share.Voice : Share.Song, songMenu.key,
                               table.songs.data(index, SongListModel.TitleRole) ?? "",
                               table.songs.data(index, table.voices ? SongListModel.AlbumTitleRole
                                                                    : SongListModel.ArtistTextRole)
                               ?? "");
            }
        }
        MenuItem {
            visible: table.removable
            height: visible ? implicitHeight : 0
            glyph: Symbols.playlist_remove
            text: songMenu.songCount > 1 ? qsTr("从歌单移除选中的 %1 首").arg(songMenu.songCount)
                                         : qsTr("从歌单移除")
            trailingText: "Delete"
            onTriggered: table.removeRequested(songMenu.keys)
        }
        MenuSeparator {}
        MenuItem {
            id: albumItem
            visible: table.showAlbumAction
            height: visible ? implicitHeight : 0
            enabled: songMenu.albumKey !== ""
            glyph: Symbols.album
            text: qsTr("查看专辑")
            onTriggered: table.albumRequested(songMenu.albumKey)
        }
        MenuItem {
            visible: table.voices
            height: visible ? implicitHeight : 0
            enabled: songMenu.albumKey !== ""
            glyph: Symbols.podcasts
            text: qsTr("查看播客")
            onTriggered: table.podcastRequested(songMenu.albumKey)
        }
        Instantiator {
            model: songMenu.artists
            delegate: MenuItem {
                required property var modelData
                enabled: modelData.key !== ""
                glyph: Symbols.person
                text: qsTr("查看歌手：%1").arg(modelData.name)
                onTriggered: table.artistRequested(modelData.key)
            }
            // After "查看专辑".
            onObjectAdded: (index, object) => {
                let base = songMenu.count;
                for (let i = 0; i < songMenu.count; ++i) {
                    if (songMenu.itemAt(i) === albumItem)
                        base = i + 1;
                }
                songMenu.insertItem(base + index, object as MenuItem);
            }
            onObjectRemoved: (index, object) => songMenu.removeItem(object as MenuItem)
        }
    }
}
