pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The library (docs/ui/pages/library.md §2): song lists on top as cards (the liked
// playlist, recently played, the listening ranking), each opening its page; then the
// collections in tabs (playlists, albums, artists, podcasts) that pin to the top once
// scrolled there, each loading when first shown and paging at the end. Playlists
// filter to the account's own or saved ones and can be created here. Guests get the
// sign-in prompt. Ctrl+PageUp/PageDown switch tabs; the shell remembers the tab.
Item {
    id: page

    required property AppShell shell

    readonly property int tab: shell.libraryTab
    readonly property int tabCount: 4
    readonly property real tabsHeight: 48
    readonly property bool wide: width >= 840
    readonly property real contentX: Math.max(24, (width - 1200) / 2)
    readonly property real contentWidth: Math.min(width - 48, 1200)
    readonly property LibraryCollectionsModel tabModel: modelOf(tab)
    // The header's tabs have scrolled under the top: the pinned copy shows.
    readonly property bool tabsPinned: headerTabs.y - flick.contentY < 0

    // By index: in onTabChanged the tabModel binding may not have followed yet.
    function modelOf(index: int): LibraryCollectionsModel {
        return [overview.playlists, overview.albums, overview.artists, overview.podcasts][index];
    }

    function showTab(): void {
        modelOf(tab).ensureLoaded();
        // With the tabs pinned, a new tab starts at its top, under them.
        if (tabsPinned)
            Qt.callLater(() => flick.contentY = headerTabs.y);
    }
    onTabChanged: showTab()

    // Back on the page (after saving an album, creating a playlist elsewhere…): what
    // has loaded is fetched again, keeping it on screen meanwhile.
    onVisibleChanged: {
        if (!visible || overview.state !== LoadState.Ready)
            return;
        overview.refresh();
        for (let i = 0; i < tabCount; ++i) {
            const model = modelOf(i);
            if (model.state === LoadState.Ready || model.state === LoadState.Partial)
                model.refresh();
        }
    }

    function loadMoreIfNear(): void {
        const model = tabModel;
        if (model.canLoadMore && !model.loadingMore && model.loadMoreError.kind < 0
                && flick.contentY + flick.height > flick.contentHeight - 600)
            model.loadMore();
    }

    // Plays the liked playlist: its songs load when asked.
    PlaylistPageModel {
        id: likedPlaylist
        property bool playWhenReady: false
        onStateChanged: {
            if (playWhenReady && (state === LoadState.Ready || state === LoadState.Partial)) {
                playWhenReady = false;
                if (songs.count > 0)
                    App.player.playSongs(songs.keys(), 0, Playback.FromPlaylist, playlistKey, title);
            }
        }
    }
    function playLiked(): void {
        likedPlaylist.playWhenReady = true;
        if (likedPlaylist.playlistKey === App.library.likedPlaylistKey)
            likedPlaylist.refresh();
        likedPlaylist.playlistKey = App.library.likedPlaylistKey;
    }

    LibraryOverviewModel {
        id: overview
        onStateChanged: {
            if (overview.state === LoadState.Ready)
                page.showTab();
        }
        onNotice: message => page.shell.notify(message)
        onLibraryChanged: page.shell.refreshLibrary()
        onPlaylistCreated: key => page.shell.openPlaylist(key)
    }

    Shortcut {
        sequences: App.shortcuts.keys.previousTab
        enabled: page.visible && overview.state === LoadState.Ready
        onActivated: page.shell.libraryTab = (page.tab + page.tabCount - 1) % page.tabCount
    }
    Shortcut {
        sequences: App.shortcuts.keys.nextTab
        enabled: page.visible && overview.state === LoadState.Ready
        onActivated: page.shell.libraryTab = (page.tab + 1) % page.tabCount
    }

    PageState {
        id: pageState
        anchors.fill: parent
        model: overview
        signedOutText: qsTr("登录后，喜欢的歌、收藏的歌单都在这里")
        onSignInRequested: page.shell.openLogin()
        skeleton: Column {
            x: page.contentX
            y: 24
            width: page.contentWidth
            spacing: 24
            Skeleton {
                width: 120
                height: 32
            }
            Row {
                spacing: 16
                Repeater {
                    model: 3
                    Skeleton {
                        required property int index
                        width: index === 0 ? 360 : 240
                        height: 160
                        radius: Theme.shape.extraLarge
                    }
                }
            }
        }
    }

    // A song list as a card: the liked playlist (primary container, plays), recently
    // played and the ranking (surface container). `strip`: the 72 px row of narrow
    // windows, like the home page's FM strip. The icon sits on a tonal circle in the tone
    // of the page it opens (expressive.md §4); on the liked card it is inverted.
    component ListCard: T.AbstractButton {
        id: card

        property string glyph
        property int tone: Md.Secondary
        property string subtitle
        property bool hero: false
        property bool strip: !page.wide
        property Component trailing: null
        readonly property color contentColor: hero ? Theme.color.onPrimaryContainer
                                                   : Theme.color.onSurface

        height: strip ? 72 : 160
        focusPolicy: Qt.TabFocus
        Accessible.name: subtitle !== "" ? text + "，" + subtitle : text

        HoverHandler {
            enabled: card.hoverEnabled
            cursorShape: Qt.PointingHandCursor
        }
        contentItem: Item {
            IconContainer {
                id: cardIcon
                x: card.strip ? 16 : 24
                y: card.strip ? (parent.height - height) / 2 : 24
                glyph: card.glyph
                size: card.strip ? 40 : 56
                tone: card.hero ? Md.Primary : card.tone
                inverted: card.hero
            }
            Column {
                x: card.strip ? cardIcon.x + cardIcon.width + 16 : 24
                y: card.strip ? (parent.height - height) / 2 : parent.height - height - 24
                width: (trailingLoader.active ? trailingLoader.x - 16 : parent.width - 24) - x
                spacing: 2
                Text {
                    width: parent.width
                    text: card.text
                    elide: Text.ElideRight
                    color: card.contentColor
                    font: card.strip ? Theme.type.titleMedium.font : Theme.type.titleLargeEmphasized.font
                }
                Text {
                    width: parent.width
                    visible: text !== ""
                    text: card.subtitle
                    elide: Text.ElideRight
                    color: card.hero ? Theme.color.onPrimaryContainer : Theme.color.onSurfaceVariant
                    font: Theme.type.bodyMedium.font
                }
            }
            Loader {
                id: trailingLoader
                x: parent.width - width - (card.strip ? 16 : 24)
                y: card.strip ? (parent.height - height) / 2 : parent.height - height - 24
                active: card.trailing !== null
                sourceComponent: card.trailing
            }
        }

        background: Rectangle {
            radius: card.strip ? Theme.shape.large : Theme.shape.extraLarge
            color: card.hero ? Theme.color.primaryContainer : Theme.color.surfaceContainerHigh
            StateLayer {
                anchors.fill: parent
                radius: parent.radius
                color: card.contentColor
                hovered: card.hovered
                pressed: card.pressed
                pressX: card.pressed ? card.pressX : width / 2
                pressY: card.pressed ? card.pressY : height / 2
            }
            FocusRing {
                shown: card.visualFocus
                containerRadius: parent.radius
            }
        }
    }

    // The collection tabs, with their counts once known; in the content and pinned.
    component Tabs: TabBar {
        id: tabBar
        width: Math.min(parent ? parent.width : 0, 480)
        Component.onCompleted: currentIndex = page.tab
        onCurrentIndexChanged: page.shell.libraryTab = currentIndex
        Connections {
            target: page
            function onTabChanged() {
                tabBar.currentIndex = page.tab;
            }
        }
        CountedTab {
            name: qsTr("歌单")
            model: overview.playlists
        }
        CountedTab {
            name: qsTr("专辑")
            model: overview.albums
        }
        CountedTab {
            name: qsTr("歌手")
            model: overview.artists
        }
        CountedTab {
            name: qsTr("播客")
            model: overview.podcasts
        }
    }
    component CountedTab: TabButton {
        property string name
        property PagedModel model
        // No number for none, like the search tabs.
        text: model.total > 0 ? qsTr("%1 %2").arg(name).arg(App.format.count(model.total)) : name
    }

    Flickable {
        id: flick
        anchors.fill: parent
        opacity: pageState.contentOpacity
        visible: opacity > 0
        contentHeight: content.implicitHeight + 24
        bottomMargin: PageInsets.bottom
        boundsBehavior: Flickable.DragAndOvershootBounds
        ScrollBar.vertical: ScrollBar {}
        onContentYChanged: page.loadMoreIfNear()
        onContentHeightChanged: Qt.callLater(page.loadMoreIfNear)

        Column {
            id: content
            x: page.contentX
            y: 24
            width: page.contentWidth
            spacing: 16

            Text {
                text: qsTr("音乐库")
                color: Theme.color.onSurface
                font: Theme.type.headlineMediumEmphasized.font
                Accessible.role: Accessible.Heading
            }

            // The cards, then the collections, rise in when they first arrive.
            Reveal {
                target: listCards
                shown: pageState.showsContent
            }
            Reveal {
                target: collections
                shown: collections.visible
                order: 1
            }

            // Song lists: a row of cards when wide, strips under each other otherwise.
            Flow {
                id: listCards
                width: parent.width
                spacing: page.wide ? 16 : 8

                ListCard {
                    id: likedCard
                    width: page.wide ? (parent.width - 48) * 0.34 : parent.width
                    hero: true
                    enabled: App.library.likedPlaylistKey !== ""
                    glyph: Symbols.favorite
                    text: qsTr("我喜欢的音乐")
                    subtitle: qsTr("%1 首").arg(App.library.likedCount)
                    onClicked: page.shell.openPlaylist(App.library.likedPlaylistKey)
                    trailing: IconButton {
                        variant: Md.Filled
                        size: likedCard.strip ? Md.Small : Md.Medium
                        enabled: App.library.likedCount > 0
                        glyph: Symbols.play_arrow
                        iconFill: 1
                        text: qsTr("播放我喜欢的音乐")
                        onClicked: page.playLiked()
                    }
                }
                ListCard {
                    width: page.wide ? (parent.width - 48) * 0.22 : parent.width
                    glyph: Symbols.history
                    text: qsTr("最近播放")
                    subtitle: overview.lastPlayedTitle !== ""
                              ? qsTr("上次听：%1").arg(overview.lastPlayedTitle) : ""
                    onClicked: page.shell.openRecent()
                }
                ListCard {
                    width: page.wide ? (parent.width - 48) * 0.22 : parent.width
                    glyph: Symbols.leaderboard
                    tone: Md.Tertiary
                    text: qsTr("听歌排行")
                    subtitle: overview.weekTopTitle !== ""
                              ? qsTr("本周：%1").arg(overview.weekTopTitle)
                              : qsTr("看看这周常听什么")
                    onClicked: page.shell.openRanking()
                }
                ListCard {
                    width: page.wide ? (parent.width - 48) * 0.22 : parent.width
                    glyph: Symbols.bar_chart
                    tone: Md.Primary
                    text: qsTr("听歌统计")
                    subtitle: weekStats.state === LoadState.Ready && !weekStats.periodEmpty
                              ? qsTr("本周 %1").arg(weekStats.durationText) : ""
                    onClicked: page.shell.openStats()
                    // This week's time; follows new plays by itself.
                    StatsPageModel {
                        id: weekStats
                        songLimit: 5
                    }
                }
            }

            Item {
                id: headerTabs
                width: parent.width
                height: page.tabsHeight
                Tabs {
                    // Hidden while the pinned copy shows, so they don't both show.
                    opacity: page.tabsPinned ? 0 : 1
                }
            }

            // Playlists: the account's own or saved ones (single-select filter chips),
            // and creating one.
            Item {
                width: parent.width
                height: 40
                visible: page.tab === 0
                ChipGroup {
                    width: newPlaylistButton.x - 16
                    anchors.verticalCenter: parent.verticalCenter
                    model: [qsTr("全部"), qsTr("创建的"), qsTr("收藏的")]
                    currentIndex: overview.playlists.filter
                    onActivated: index => overview.playlists.filter = index
                }
                Button {
                    id: newPlaylistButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    variant: Md.Tonal
                    glyph: Symbols.add
                    text: qsTr("新建歌单")
                    onClicked: newPlaylistDialog.open()
                }
            }

            TabState {
                model: page.tabModel
                emptyGlyph: [Symbols.queue_music, Symbols.album, Symbols.person,
                             Symbols.podcasts][page.tab]
                emptyText: [qsTr("还没有歌单。新建一个，或者在歌单页点「收藏」"),
                            qsTr("还没有收藏的专辑。在专辑页点「收藏」就会出现在这里"),
                            qsTr("还没有关注的歌手。在歌手页点「关注」就会出现在这里"),
                            qsTr("还没有收藏的播客。在播客页点「收藏播客」就会出现在这里")][page.tab]
            }
            StateMessage {
                width: parent.width
                topPadding: 24
                visible: page.tab === 0 && overview.playlists.state === LoadState.Ready
                         && overview.playlists.filter !== LibraryCollectionsModel.All
                         && overview.playlists.items.count === 0
                         && !overview.playlists.canLoadMore
                glyph: Symbols.queue_music
                message: overview.playlists.filter === LibraryCollectionsModel.Created
                         ? qsTr("还没有创建的歌单") : qsTr("还没有收藏的歌单")
            }
            CollectionGrid {
                id: collections
                x: -8
                width: parent.width + 16
                visible: page.tabModel.state === LoadState.Ready
                         || page.tabModel.state === LoadState.Partial
                model: page.tabModel.items
                onActivated: (kind, key) => page.shell.openCollection(kind, key)
            }
            LoadMoreFooter {
                width: parent.width
                model: page.tabModel
            }
        }
    }

    // The tabs, pinned at the top once the content's have scrolled there.
    Rectangle {
        width: parent.width - 12
        height: page.tabsHeight
        visible: page.tabsPinned && flick.visible
        color: Theme.color.surface

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
        }
        HoverHandler {
            blocking: true
        }
        Item {
            x: page.contentX
            width: page.contentWidth
            height: parent.height
            Tabs {}
        }
    }

    NewPlaylistDialog {
        id: newPlaylistDialog
        onCreateRequested: title => overview.createPlaylist(title)
    }
}
