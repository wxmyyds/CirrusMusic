pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The layout playlist, album and daily songs pages share (docs/ui/pages/playlist.md,
// album-daily.md): a header that scrolls away (cover, the page's lines, play all and
// its actions, the in-page find) over the song table. Once the header has gone, a
// compact bar with the title, play all and the find stays on top, the column titles
// under it. Ctrl+F (AppShell) calls find(); typing on the table finds too.
// Pages with tabs (artist.md) add a tab row under the actions, pinned under the
// compact bar once scrolled there, and show other content than songs in `bodyContent`.
// Podcasts (search.md §3.4) list their voices in the same table (`voices`).
// Wide pages (`twoPane`, docs/ui/redesign-md3e.md §7) take the cover, the page's lines and
// the actions out of the table's header into a pane on the left that stays put, in the
// cover's colors (CoverScheme); the find field and the tabs stay on top of the table.
// The header's parts are components, since they live inside the table's header:
//   SongCollectionPage { model: pageModel; songs: pageModel.songs; title: …
//       infoContent: Column { … }; actionsContent: Row { … } }
Item {
    id: page

    required property AppShell shell
    required property LoadableModel model
    required property SongListModel songs
    property string title
    // Where the songs come from (Playback.Origin, its id and name), for the listening
    // reports and the local statistics.
    property int origin: Playback.NoOrigin
    property string originId
    property string originName: title
    // What 复制链接 in the top bar copies (pages/share.md §3): a Share.Kind and its key; -1
    // for pages without a link of their own.
    property int shareKind: -1
    property string shareKey
    property string shareAuthor // artists, creator; empty for the rest
    property string coverKey
    property string coverGlyph: Symbols.queue_music
    // Drawn instead of the cover art (fills the size it gets: 200, 140 or 32).
    property Component cover: null
    // A round cover (artists): the skeleton matches.
    property bool roundCover: false
    // The cover a card's cover flies to (CoverFlight, expressive.md §5): the pane's or
    // the header's.
    readonly property Item flightCover: twoPane ? paneCover : headerCover
    property Item headerCover: null
    property Item paneCover: null
    readonly property bool flightRound: roundCover
    readonly property real flightRadius: twoPane ? Theme.shape.extraLarge : Theme.shape.large
    readonly property bool flightReady: state_.contentOpacity > 0.99
                                        && ((flightCover as Cover)?.ready ?? true)
    // Right of the cover: the type label, title and the page's lines.
    property Component infoContent: null
    // Under the cover, full width (e.g. a description in narrow windows), while
    // `showExtra`.
    property Component extraContent: null
    property bool showExtra: true
    // After "播放全部": a Flow of buttons (one row in the header, wrapping in the pane).
    property Component actionsContent: null
    // Under the actions (notices), while `showNotice`.
    property Component noticeContent: null
    property bool showNotice: true
    // Tabs under the actions, `tabsHeight` tall; instantiated twice (in the header and
    // pinned under the compact bar), so bind them to a page property.
    property Component tabsContent: null
    readonly property real tabsHeight: 48
    // Under the tabs, while `showBody`: a tab's content that is not the song table.
    property Component bodyContent: null
    property bool showBody: false
    // Whether the song table (column titles and rows) shows.
    property bool showSongs: true
    // Under the rows (e.g. the next page loading).
    property Component footerContent: null
    // Whether there is an in-page find (AppShell's Ctrl+F falls back to the search).
    property bool findable: true
    // The play button: its text and what it plays (the shown order when null).
    property string playText: qsTr("播放全部")
    // The play button (header and compact bar); pages that aren't a song list leave it out.
    property bool showPlay: true
    // The action row (play, actionsContent, find); a page with nothing in it hides it.
    property bool showActions: true
    // Shown in the compact bar, left of its play button and find field.
    property Component compactActionsContent: null
    property SongListModel playSource: null
    // The widths of the action skeletons, play all first.
    property list<int> actionSkeletons: [116]
    property string findPlaceholder: qsTr("在歌单中查找")
    // %1 is the text found.
    property string noMatchText: qsTr("歌单里没有与「%1」相符的歌")
    property string emptyGlyph: Symbols.queue_music
    property string emptyText: qsTr("这里还没有歌")
    // What a guest is told (pages that need signing in).
    property string signedOutText

    // Passed to the table.
    property bool sortable: true
    property bool removable: false
    property bool showCover: true
    property bool albumColumn: true
    property bool reasonColumn: false
    property string reasonTitle: qsTr("推荐理由")
    property bool numberByPosition: false
    property bool showAlbumAction: true
    property bool showLiked: true
    // Podcast voices instead of songs (search.md §3.4): they play with playVoices.
    property bool voices: false
    // What the play button plays is voices (off when it plays a list of songs while
    // the table shows voices, as on the recent page).
    property bool playsVoices: voices
    // Ranks (the listening ranking): the first three numbers in primary.
    property bool ranked: false

    readonly property alias table: songTable
    readonly property alias sorted: sortedModel
    readonly property alias pageState: state_
    // Below this the header stacks (playlist.md §3.2).
    readonly property bool wide: width >= 840
    // The info pane beside the table, and its width.
    readonly property bool twoPane: width >= 1000
    readonly property real paneWidth: Math.round(Math.max(320, Math.min(380, width * 0.3)))
    // The pane's actions are as tall as its play button (smaller in short windows).
    readonly property int paneActionSize: pane.compact ? Md.Medium : Md.Large
    // The title's type: heavy and large in the pane (redesign-md3e.md §7.1).
    readonly property font titleFont: twoPane ? Fonts.font(800, 44)
                                    : wide ? Theme.type.headlineLargeEmphasized.font
                                           : Theme.type.headlineSmallEmphasized.font
    // The colors `infoContent` and `extraContent` write in: on the page, or on the pane
    // (a container in the cover's colors).
    readonly property color infoColor: twoPane ? tint.scheme.onPrimaryContainer
                                               : Theme.color.onSurface
    readonly property color infoVariantColor: twoPane ? Qt.alpha(tint.scheme.onPrimaryContainer, 0.8)
                                                      : Theme.color.onSurfaceVariant
    readonly property color infoAccentColor: twoPane ? tint.scheme.onPrimaryContainer
                                                     : Theme.color.primary
    // Something for the bar over the table to hold while `twoPane`.
    readonly property bool hasToolbar: findable && hasSongs
    readonly property bool hasSongs: songs.count > 0
    // The in-page find, shared by the header's field and the compact bar's.
    property string findText
    // Set by the header (it lives inside the table's header).
    property Item headerContent: null
    property Item headerActions: null
    property Item headerTabs: null
    property SearchBar headerFind: null
    readonly property bool canPlay: (playSource ? playSource.count > 0 : hasSongs)
                                    && (!playsVoices || App.player.canPlayVoices)
    // 0–1 as the header's action row scrolls under the top: the compact bar slides in.
    // Without the action row, the tabs take its place.
    readonly property Item compactAnchor: showActions ? headerActions : headerTabs
    // With the pane the bar is always there, holding the find field only.
    readonly property real compactProgress: twoPane ? 1 : compactAnchor
        ? Math.max(0, Math.min(1, (songTable.scrolled - compactAnchor.y - compactAnchor.height
                                   + 96) / 56)) : 0
    // The header's tabs have scrolled under the compact bar: the pinned copy shows.
    readonly property bool tabsPinned: tabsContent !== null
                                       && (twoPane || (headerTabs !== null
                                                       && headerTabs.y - songTable.scrolled
                                                          < compactBar.y + compactBar.height))

    signal removeRequested(list<string> keys)

    function find(): void {
        const field = compactProgress > 0.5 ? compactFind : headerFind;
        if (!field)
            return;
        field.forceActiveFocus(Qt.ShortcutFocusReason);
        field.selectAll();
    }

    // The songs as the page shows them (sorted, found), what playAll() plays.
    function shownKeys(): list<string> {
        return playSource ? playSource.keys() : sortedModel.keys();
    }

    function playAll(): void {
        const keys = shownKeys();
        if (keys.length === 0)
            return;
        if (playsVoices)
            App.player.playVoices(keys, 0);
        else
            App.player.playSongs(keys, 0, page.origin, page.originId, page.originName);
    }

    clip: true

    SortedSongsModel {
        id: sortedModel
        source: page.showSongs ? page.songs : null
        filterText: page.findText
    }

    // The pane's colors: the cover's.
    CoverScheme {
        id: tint
        cover: page.twoPane && page.coverKey !== ""
               ? "image://cover/" + encodeURIComponent(page.coverKey) : ""
        dark: Theme.dark
    }

    // The info pane (docs/ui/redesign-md3e.md §7): cover, the page's lines, the play
    // button at the hero's size and the actions. It ends above the mini player; what
    // does not fit scrolls inside it.
    Rectangle {
        id: pane
        readonly property real inner: width - 64
        // The height there is: down to the mini player.
        readonly property real room: page.height - y - Math.max(24, PageInsets.bottom)
        // Short windows: a smaller play button.
        readonly property bool compact: room < 640
        readonly property bool loading: page.model.state === LoadState.Loading
                                        && state_.contentOpacity < 0.01
        ColorScope.scheme: tint.scheme
        x: 32
        y: 16
        width: page.paneWidth
        height: Math.min(paneFlick.contentHeight, room)
        visible: page.twoPane && opacity > 0
        opacity: loading ? 1 : state_.contentOpacity
        radius: 48
        color: ColorScope.scheme.primaryContainer
        clip: true

        Flickable {
            id: paneFlick
            anchors.fill: parent
            contentHeight: (pane.loading ? paneSkeleton.height : paneColumn.height) + 64
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: paneColumn
                x: 32
                y: 32
                width: pane.inner
                spacing: 18
                visible: !pane.loading

                Cover {
                    id: paneCoverItem
                    // What is under it, and the size that leaves that in view.
                    readonly property real rest: paneInfo.height + paneColumn.spacing
                        + (paneExtra.wanted ? paneExtra.height + paneColumn.spacing : 0)
                        + (page.showActions ? paneActions.height + paneColumn.spacing : 0)
                    readonly property real size: Math.round(
                        Math.max(120, Math.min(pane.inner, pane.room - 64 - rest)))
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: size
                    height: size
                    radius: page.roundCover ? width / 2 : Theme.shape.extraLarge
                    Component.onCompleted: page.paneCover = paneCoverItem
                }
                Loader {
                    id: paneInfo
                    width: parent.width
                    active: page.twoPane
                    sourceComponent: page.infoContent
                }
                Loader {
                    id: paneExtra
                    readonly property bool wanted: page.extraContent !== null && page.showExtra
                    width: parent.width
                    visible: wanted
                    active: page.twoPane
                    sourceComponent: page.extraContent
                }
                // The page's main action at the hero's size; the others (square icon
                // buttons as tall, PageAction) beside it, or on the next line.
                Flow {
                    id: paneActions
                    width: parent.width
                    visible: page.showActions
                    spacing: 8
                    Button {
                        // Actions as tall as it (PageActions) stay on its row: its side
                        // padding gives way for them (#9). Others (button groups) go under it.
                        readonly property real beside: paneOthers.height === height
                                                        ? paneOthers.width + paneActions.spacing : 0
                        readonly property real widest: pane.compact ? sidePadding : 40
                        visible: page.showPlay
                        enabled: page.canPlay
                        variant: Md.Filled
                        size: pane.compact ? Md.Medium : Md.Large
                        glyph: Symbols.play_arrow
                        iconFill: 1
                        // Short: the pane says what it plays.
                        text: qsTr("播放")
                        font: pane.compact ? labelStyle.font : Fonts.font(700, 24)
                        // 172 wide at Large; narrower panes take it down to 8 a side, where
                        // two square actions still fit beside it.
                        leftPadding: Math.max(8, Math.min(widest, Math.floor(
                            (paneActions.width - beside - implicitContentWidth) / 2)))
                        rightPadding: leftPadding
                        width: implicitContentWidth + leftPadding + rightPadding
                        Accessible.name: page.playText
                        onClicked: page.playAll()
                    }
                    // At its own size: the actions' Flow keeps one row.
                    Loader {
                        id: paneOthers
                        active: page.twoPane
                        sourceComponent: page.actionsContent
                    }
                }
            }

            Column {
                id: paneSkeleton
                x: 32
                y: 32
                width: pane.inner
                spacing: 20
                visible: pane.loading

                component Bone: Skeleton {
                    color: Qt.alpha(ColorScope.scheme.onPrimaryContainer, 0.12)
                }
                Bone {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: paneCoverItem.size
                    height: width
                    radius: page.roundCover ? width / 2 : Theme.shape.extraLarge
                }
                Bone {
                    width: 40
                    height: 12
                }
                Bone {
                    width: Math.min(240, pane.inner)
                    height: 32
                }
                Bone {
                    width: Math.min(200, pane.inner)
                    height: 14
                }
                Bone {
                    width: pane.compact ? 140 : 200
                    height: pane.compact ? 56 : 96
                    radius: height / 2
                }
            }
        }
    }

    PageState {
        id: state_
        anchors.fill: parent
        anchors.leftMargin: songTable.anchors.leftMargin
        model: page.model
        emptyInContent: true
        signedOutText: page.signedOutText
        onSignInRequested: page.shell.openLogin()
        // Laid out like the header and the table below (playlist.md §6).
        skeleton: SongTableSkeleton {
            table: songTable
            headerSkeleton: Column {
                topPadding: 24
                bottomPadding: 16
                spacing: 24

                Item {
                    x: songTable.rowX
                    width: songTable.rowWidth
                    height: skeletonCover.height
                    visible: !page.twoPane

                    Skeleton {
                        id: skeletonCover
                        width: page.wide ? 200 : 140
                        height: width
                        radius: page.roundCover ? width / 2 : Theme.shape.large
                    }
                    Column {
                        readonly property real available: parent.width - x
                        x: skeletonCover.width + 24
                        y: page.wide ? 4 : (skeletonCover.height - height) / 2
                        spacing: 12
                        Skeleton {
                            width: 32
                            height: 12
                        }
                        Skeleton {
                            width: Math.min(280, parent.available)
                            height: page.wide ? 32 : 24
                        }
                        Skeleton {
                            width: Math.min(200, parent.available)
                            height: 14
                        }
                        Skeleton {
                            visible: page.wide
                            width: Math.min(360, parent.available)
                            height: 14
                        }
                        Skeleton {
                            width: Math.min(220, parent.available)
                            height: 14
                        }
                    }
                }
                // Actions and the find field.
                Item {
                    x: songTable.rowX
                    width: songTable.rowWidth
                    height: 40
                    Row {
                        spacing: 8
                        visible: !page.twoPane
                        Repeater {
                            model: page.actionSkeletons
                            Skeleton {
                                required property int modelData
                                width: modelData
                                height: 40
                                radius: height / 2
                            }
                        }
                    }
                    Skeleton {
                        x: page.twoPane ? 0 : parent.width - width
                        anchors.verticalCenter: parent.verticalCenter
                        width: page.twoPane ? Math.min(400, parent.width)
                                            : Math.min(240, parent.width / 3)
                        height: page.twoPane ? 48 : 36
                        radius: height / 2
                    }
                }
            }
        }
    }

    // Find field behaviour, for both fields: Esc clears, then returns to the table;
    // ↓ and Enter go to the table.
    component FindField: SearchBar {
        id: field
        height: 36
        leftPadding: 44
        handlesEscape: false
        font: Theme.type.bodyMedium.font
        placeholderText: page.findPlaceholder
        Accessible.name: page.findPlaceholder
        text: page.findText
        onTextChanged: page.findText = text
        Keys.onEscapePressed: {
            if (length > 0)
                clear();
            else
                songTable.focusTable();
        }
        Keys.onDownPressed: songTable.focusTable()
        onAccepted: songTable.focusTable()
    }

    // The cover, or what the page draws instead.
    component Cover: Item {
        id: coverItem
        property real radius: Theme.shape.large
        // Its picture is shown (or there is none).
        readonly property bool ready: page.cover ? ((custom.item as CoverArt)?.ready ?? true)
                                                 : art.ready || page.coverKey === ""
        CoverArt {
            id: art
            anchors.fill: parent
            visible: !page.cover
            radius: coverItem.radius
            glyph: page.coverGlyph
            coverKey: page.coverKey
        }
        Loader {
            id: custom
            anchors.fill: parent
            active: page.cover !== null
            sourceComponent: page.cover
        }
    }

    SongTable {
        id: songTable
        origin: page.origin
        originId: page.originId
        originName: page.originName
        anchors.fill: parent
        // Rows 12 from the pane and 32 from the panel's edge (the table keeps 24).
        anchors.leftMargin: page.twoPane ? pane.x + pane.width - 12 : 0
        anchors.rightMargin: page.twoPane ? 8 : 0
        opacity: state_.contentOpacity
        visible: opacity > 0
        songs: sortedModel
        pinnedTop: compactBar.y + compactBar.height + (page.tabsContent ? page.tabsHeight : 0)
        // The pinned titles start beside the pane, like the compact bar.
        pinnedLeft: page.twoPane ? pane.x + pane.width - x : 0
        // No titles over an empty list.
        showColumnTitles: page.showSongs && page.hasSongs
        footerContent: page.footerContent
        sortable: page.sortable
        removable: page.removable
        showCover: page.showCover
        albumColumn: page.albumColumn
        reasonColumn: page.reasonColumn
        reasonTitle: page.reasonTitle
        numberByPosition: page.numberByPosition
        showAlbumAction: page.showAlbumAction
        showLiked: page.showLiked
        voices: page.voices
        ranked: page.ranked
        onPodcastRequested: key => page.shell.openPodcast(key)
        onRemoveRequested: keys => page.removeRequested(keys)
        onAddToPlaylistRequested: keys => page.shell.addToPlaylist(keys)
        onSignInRequested: page.shell.openLogin()
        onAlbumRequested: key => page.shell.openAlbum(key)
        onArtistRequested: key => page.shell.openArtist(key)
        onSongRequested: key => page.shell.openSong(key)
        onCommentsRequested: (key, title, artistText, coverKey) =>
                             page.shell.openSongComments(key, title, artistText, coverKey)
        // Typing on the table goes on in the find field.
        typingFinds: page.findable && page.hasSongs
        onTyped: text => {
            const field = page.compactProgress > 0.5 ? compactFind : page.headerFind;
            if (!field)
                return;
            field.forceActiveFocus(Qt.ShortcutFocusReason);
            field.insert(field.length, text);
            field.cursorPosition = field.length;
        }

        headerContent: Column {
            id: header

            // With the pane, the rows start under the bar over the table (and the tabs).
            topPadding: page.twoPane ? compactBar.height
                                       + (page.tabsContent ? page.tabsHeight + 8 : 0) : 24
            bottomPadding: 16
            spacing: 24
            Component.onCompleted: page.headerContent = header

            Item {
                x: songTable.rowX
                width: songTable.rowWidth
                height: Math.max(cover.height, info.height)
                visible: !page.twoPane

                Cover {
                    id: cover
                    width: page.wide ? 200 : 140
                    height: width
                    Component.onCompleted: page.headerCover = cover
                }
                Loader {
                    id: info
                    x: cover.width + 24
                    width: parent.width - x
                    anchors.verticalCenter: page.wide ? undefined : cover.verticalCenter
                    active: !page.twoPane
                    sourceComponent: page.infoContent
                }
            }

            Loader {
                x: songTable.rowX
                width: songTable.rowWidth
                visible: page.extraContent !== null && page.showExtra && !page.twoPane
                active: !page.twoPane
                sourceComponent: page.extraContent
            }

            // Actions, and the find on the right.
            Item {
                id: actions
                x: songTable.rowX
                width: songTable.rowWidth
                height: 56
                visible: page.showActions && !page.twoPane
                Component.onCompleted: page.headerActions = actions

                // The main action is a medium button (expressive.md §3.3), the rest small.
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Button {
                        visible: page.showPlay
                        enabled: page.canPlay
                        variant: Md.Filled
                        size: Md.Medium
                        glyph: Symbols.play_arrow
                        text: page.playText
                        onClicked: page.playAll()
                    }
                    Loader {
                        anchors.verticalCenter: parent.verticalCenter
                        active: !page.twoPane
                        sourceComponent: page.actionsContent
                    }
                }
                FindField {
                    id: headerFind
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(240, parent.width / 3)
                    visible: page.findable && page.hasSongs
                    Component.onCompleted: page.headerFind = headerFind
                }
            }

            Loader {
                id: tabs
                x: songTable.rowX
                width: songTable.rowWidth
                height: page.tabsHeight
                visible: page.tabsContent !== null && !page.twoPane
                // Hidden while the pinned copy shows, so they don't both show.
                opacity: page.tabsPinned ? 0 : 1
                sourceComponent: page.tabsContent
                Component.onCompleted: page.headerTabs = tabs
            }
            Loader {
                x: songTable.rowX
                width: songTable.rowWidth
                visible: page.noticeContent !== null && page.showNotice
                sourceComponent: page.noticeContent
            }
            StateMessage {
                x: songTable.rowX
                width: songTable.rowWidth
                topPadding: 24
                // Also once the last song has been removed.
                visible: page.showSongs && state_.showsContent && !page.hasSongs
                glyph: page.emptyGlyph
                message: page.emptyText
            }
            StateMessage {
                x: songTable.rowX
                width: songTable.rowWidth
                topPadding: 24
                visible: page.showSongs && page.hasSongs && sortedModel.count === 0
                glyph: Symbols.search
                message: page.noMatchText.arg(page.findText.trim())
            }
            Loader {
                x: songTable.rowX
                width: songTable.rowWidth
                visible: page.bodyContent !== null && page.showBody
                active: visible
                sourceComponent: page.bodyContent
            }
        }
    }

    // Slides in, following the scroll, once the header's actions have gone. With the
    // pane it is the bar over the table: always there, holding the find field.
    Rectangle {
        id: compactBar
        // Beside the pane (the table reaches under its edge), leaving the table's scroll
        // bar uncovered.
        x: page.twoPane ? pane.x + pane.width : songTable.x
        width: songTable.x + songTable.width - songTable.scrollBarWidth - x
        height: page.twoPane ? (page.hasToolbar ? 72 : 16) : 56
        y: -height * (1 - page.compactProgress)
        visible: page.compactProgress > 0
        color: Theme.color.surface

        // The rows scrolling under it are not hovered or clicked through it.
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
        }
        HoverHandler {
            blocking: true
        }

        Item {
            x: songTable.x + songTable.rowX - compactBar.x
            width: songTable.rowWidth
            height: parent.height

            Cover {
                id: compactCover
                x: 8
                width: 32
                height: 32
                visible: !page.twoPane
                anchors.verticalCenter: parent.verticalCenter
                radius: page.roundCover ? width / 2 : Theme.shape.small
            }
            Text {
                x: compactCover.x + compactCover.width + 12
                width: (compactActions.item ? compactActions.x
                        : compactPlay.visible ? compactPlay.x : parent.width) - x - 12
                anchors.verticalCenter: parent.verticalCenter
                visible: !page.twoPane
                text: page.title
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: Theme.type.titleMedium.font
            }
            Loader {
                id: compactActions
                x: (compactPlay.visible ? compactPlay.x
                    : compactFind.visible ? compactFind.x : parent.width) - width - 12
                anchors.verticalCenter: parent.verticalCenter
                active: !page.twoPane
                sourceComponent: page.compactActionsContent
            }
            IconButton {
                id: compactPlay
                x: (compactFind.visible ? compactFind.x : parent.width) - width - 12
                visible: page.showPlay && !page.twoPane
                anchors.verticalCenter: parent.verticalCenter
                enabled: page.canPlay
                variant: Md.Filled
                glyph: Symbols.play_arrow
                iconFill: 1
                text: page.playText
                onClicked: page.playAll()
            }
            FindField {
                id: compactFind
                x: page.twoPane ? 0 : parent.width - width
                // Level with the pane's top, as tall as the search field.
                y: page.twoPane ? pane.y : (parent.height - height) / 2
                width: page.twoPane ? Math.min(400, sortButton.x - 16) : Math.min(240, parent.width / 3)
                height: page.twoPane ? 48 : 36
                leftPadding: page.twoPane ? 52 : 44
                font: page.twoPane ? Theme.type.bodyLarge.font : Theme.type.bodyMedium.font
                visible: page.findable && (!page.twoPane || page.hasSongs)
            }
            // The order, beside the find field (the column titles sort too).
            Button {
                id: sortButton
                readonly property list<string> names: [qsTr("默认顺序"), qsTr("按标题"),
                    qsTr("按专辑"), qsTr("按时长")]
                x: page.twoPane && visible ? parent.width - width : parent.width
                y: pane.y + 4
                visible: page.twoPane && page.sortable && page.hasSongs
                variant: Md.Tonal
                size: Md.Small
                glyph: Symbols.sort
                text: names[sortedModel.sortField]
                      + (sortedModel.sortField !== SortedSongsModel.Original && sortedModel.descending
                         ? qsTr("（倒序）") : "")
                function sortBy(field: int): void {
                    sortedModel.sortField = field;
                    sortedModel.descending = false;
                }
                onClicked: sortMenu.popup(sortButton, 0, sortButton.height + 4)

                Menu {
                    id: sortMenu
                    MenuItem {
                        checkable: true
                        checked: sortedModel.sortField === 0
                        text: sortButton.names[0]
                        onTriggered: sortButton.sortBy(0)
                    }
                    MenuItem {
                        checkable: true
                        checked: sortedModel.sortField === 1
                        text: sortButton.names[1]
                        onTriggered: sortButton.sortBy(1)
                    }
                    MenuItem {
                        checkable: true
                        checked: sortedModel.sortField === 2
                        text: sortButton.names[2]
                        onTriggered: sortButton.sortBy(2)
                    }
                    MenuItem {
                        checkable: true
                        checked: sortedModel.sortField === 3
                        text: sortButton.names[3]
                        onTriggered: sortButton.sortBy(3)
                    }
                    MenuSeparator {}
                    MenuItem {
                        enabled: sortedModel.sortField !== SortedSongsModel.Original
                        checkable: true
                        checked: sortedModel.descending
                        text: qsTr("倒序")
                        onTriggered: sortedModel.descending = !sortedModel.descending
                    }
                }
            }
        }
    }

    // The tabs, pinned under the compact bar once the header's have scrolled there.
    Rectangle {
        x: compactBar.x
        y: compactBar.y + compactBar.height
        width: compactBar.width
        height: page.tabsHeight
        visible: page.tabsPinned
        color: Theme.color.surface

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
        }
        HoverHandler {
            blocking: true
        }
        Loader {
            x: songTable.x + songTable.rowX - compactBar.x
            width: songTable.rowWidth
            height: parent.height
            active: page.tabsContent !== null
            sourceComponent: page.tabsContent
        }
    }
}
