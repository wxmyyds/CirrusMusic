pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Search results (docs/ui/pages/search.md §3): the query as the title, then tabs
// (综合, 单曲, 歌手, 专辑, 歌单, 播客, 声音) that pin to the top once scrolled there.
// Everything scrolls in one song table: the songs of 综合, 单曲 and 声音 are its rows;
// cards and states sit in its header and footer. Each tab loads when first shown;
// a new query keeps the tab. Ctrl+PageUp/PageDown switch tabs. The input is the title
// bar's search field; `search` is the shell's model. Before any search (back to an
// empty page) the trending keywords show.
Item {
    id: page

    required property AppShell shell
    required property SearchModel search

    readonly property int tab: search.tab
    readonly property int tabCount: 7
    readonly property real tabsHeight: 48
    readonly property string query: search.query
    // The current tab's model, and its songs when it lists songs or voices.
    readonly property LoadableModel tabModel: [search.overview, search.songs, search.artists,
                                               search.albums, search.playlists,
                                               search.podcasts, search.voices][tab]
    readonly property SongListModel tabSongs: tab === SearchModel.Overview
                                              ? search.overview.songs
                                              : tab === SearchModel.Songs ? search.songs.songs
                                              : tab === SearchModel.Voices ? search.voices.songs
                                              : null
    readonly property CollectionResults tabCollections: tab === SearchModel.Artists
                                                        ? search.artists
                                                        : tab === SearchModel.Albums
                                                          ? search.albums
                                                        : tab === SearchModel.Playlists
                                                          ? search.playlists
                                                        : tab === SearchModel.Podcasts
                                                          ? search.podcasts : null
    readonly property bool ready: tabModel.state === LoadState.Ready
                                  || tabModel.state === LoadState.Partial
    readonly property bool showSongs: ready && tabSongs !== null && tabSongs.count > 0
    readonly property bool voices: tab === SearchModel.Voices
    // Set by the header (it lives inside the table's header).
    property Item headerTabs: null
    // The header's tabs have scrolled under the top: the pinned copy shows.
    readonly property bool tabsPinned: headerTabs !== null
                                       && headerTabs.y - table.scrolled < 0

    function playSongs(keys: list<string>): void {
        if (keys.length === 0)
            return;
        if (voices)
            App.player.playVoices(keys, 0);
        else
            App.player.playSongs(keys, 0, Playback.FromSearch, search.query);
    }

    // With the tabs pinned, a new tab starts at its top, under them.
    function showTab(): void {
        if (tabsPinned && headerTabs)
            Qt.callLater(() => {
                const view = table.view;
                view.contentY = view.originY + page.headerTabs.y;
            });
    }
    onTabChanged: showTab()

    // The next page once the end is near: rows for songs, the scroll for cards.
    function loadMoreIfNear(): void {
        const view = table.view;
        const model = tab === SearchModel.Songs ? search.songs
                    : tab === SearchModel.Voices ? search.voices : tabCollections;
        if (!model || !model.canLoadMore || model.loadingMore || model.loadMoreError.kind >= 0)
            return;
        const near = tabSongs ? table.lastVisibleRow >= tabSongs.count - 20
                              : view.contentY + view.height
                                > view.originY + view.contentHeight - 600;
        if (near)
            model.loadMore();
    }
    Connections {
        target: table.view
        function onContentYChanged() {
            page.loadMoreIfNear();
        }
        function onContentHeightChanged() {
            Qt.callLater(page.loadMoreIfNear);
        }
    }

    Shortcut {
        sequences: App.shortcuts.keys.previousTab
        enabled: page.visible && page.query !== ""
        onActivated: page.search.tab = (page.tab + page.tabCount - 1) % page.tabCount
    }
    Shortcut {
        sequences: App.shortcuts.keys.nextTab
        enabled: page.visible && page.query !== ""
        onActivated: page.search.tab = (page.tab + 1) % page.tabCount
    }

    // Plays the best match: an artist's popular songs, an album's songs. The page
    // model loads when asked.
    ArtistPageModel {
        id: bestArtist
        property bool playWhenReady: false
        onStateChanged: {
            if (playWhenReady && (state === LoadState.Ready || state === LoadState.Partial)) {
                playWhenReady = false;
                page.playSongs(topSongs.keys());
            }
        }
    }
    AlbumPageModel {
        id: bestAlbum
        property bool playWhenReady: false
        onStateChanged: {
            if (playWhenReady && (state === LoadState.Ready || state === LoadState.Partial)) {
                playWhenReady = false;
                page.playSongs(songs.keys());
            }
        }
    }
    function playBest(): void {
        const overview = search.overview;
        const model = overview.bestKind === Collection.Artist ? bestArtist : bestAlbum;
        model.playWhenReady = true;
        if (overview.bestKind === Collection.Artist) {
            if (bestArtist.artistKey === overview.bestKey)
                bestArtist.reload();
            bestArtist.artistKey = overview.bestKey;
        } else {
            if (bestAlbum.albumKey === overview.bestKey)
                bestAlbum.reload();
            bestAlbum.albumKey = overview.bestKey;
        }
    }

    SortedSongsModel {
        id: sorted
        source: page.showSongs ? page.tabSongs : null
    }

    // A tab with its total once known.
    component CountedTab: TabButton {
        property string name
        property PagedModel model
        // 综合's total for the kind until the tab has loaded (-1 for none).
        property int overviewIndex: -1
        readonly property real total: model.total >= 0 ? model.total
                                    : overviewIndex >= 0
                                      ? page.search.overview.totals[overviewIndex] : -1
        // No number for none (search.md §9).
        text: total > 0 ? qsTr("%1 %2").arg(name).arg(App.format.count(total)) : name
    }

    // The tabs, with totals once known; in the header and pinned at the top.
    component Tabs: TabBar {
        id: tabBar
        width: Math.min(parent ? parent.width : 0, 760)
        Component.onCompleted: currentIndex = page.tab
        onCurrentIndexChanged: page.search.tab = currentIndex
        Connections {
            target: page.search
            function onTabChanged() {
                tabBar.currentIndex = page.search.tab;
            }
        }
        TabButton {
            text: qsTr("综合")
        }
        CountedTab {
            name: qsTr("单曲")
            model: page.search.songs
            overviewIndex: 0
        }
        CountedTab {
            name: qsTr("歌手")
            model: page.search.artists
            overviewIndex: 1
        }
        CountedTab {
            name: qsTr("专辑")
            model: page.search.albums
            overviewIndex: 2
        }
        CountedTab {
            name: qsTr("歌单")
            model: page.search.playlists
            overviewIndex: 3
        }
        CountedTab {
            name: qsTr("播客")
            model: page.search.podcasts
        }
        CountedTab {
            name: qsTr("声音")
            model: page.search.voices
        }
    }

    // A group's title in 综合: it and its › switch to the group's tab.
    component GroupTitle: Item {
        id: groupTitle

        property string text
        property int target: -1

        width: parent ? parent.width : 0
        height: 56

        Button {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: -12
            variant: Md.Text
            text: groupTitle.text
            trailingGlyph: Symbols.chevron_right
            Accessible.name: qsTr("查看全部%1").arg(groupTitle.text)
            onClicked: page.search.tab = groupTitle.target
        }
    }

    // 综合's groups of cards, under its songs.
    component CardGroup: Column {
        id: group

        required property CollectionListModel items
        required property string title
        required property int target

        width: parent ? parent.width : 0
        visible: items.count > 0
        GroupTitle {
            text: group.title
            target: group.target
        }
        CollectionGrid {
            x: -8
            width: parent.width + 16
            model: group.items
            maximumRows: 1
            onActivated: (kind, key) => page.shell.openCollection(kind, key)
        }
    }

    SongTable {
        origin: Playback.FromSearch
        originId: page.search.query
        id: table
        anchors.fill: parent
        visible: page.query !== ""
        songs: sorted
        pinnedTop: page.tabsHeight
        showColumnTitles: page.showSongs
        sortable: false
        voices: page.voices
        showLiked: !page.voices
        albumColumn: !page.voices
        showAlbumAction: !page.voices
        onAddToPlaylistRequested: keys => page.shell.addToPlaylist(keys)
        onSignInRequested: page.shell.openLogin()
        onAlbumRequested: key => page.shell.openAlbum(key)
        onArtistRequested: key => page.shell.openArtist(key)
        onSongRequested: key => page.shell.openSong(key)
        onCommentsRequested: (key, title, artistText, coverKey) =>
                             page.shell.openSongComments(key, title, artistText, coverKey)
        onPodcastRequested: key => page.shell.openPodcast(key)

        headerContent: Column {
            topPadding: 24
            spacing: 8

            Text {
                x: table.rowX
                width: table.rowWidth
                text: page.query
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: Theme.type.headlineMediumEmphasized.font
                Accessible.role: Accessible.Heading
            }

            Item {
                id: headerTabs
                x: table.rowX
                width: table.rowWidth
                height: page.tabsHeight
                Component.onCompleted: page.headerTabs = headerTabs
                Tabs {
                    // Hidden while the pinned copy shows, so they don't both show.
                    opacity: page.tabsPinned ? 0 : 1
                }
            }

            Column {
                x: table.rowX
                width: table.rowWidth
                topPadding: 8

                TabState {
                    model: page.tabModel
                    emptyGlyph: Symbols.search
                    emptyText: [qsTr("没有找到与「%1」相关的内容"), qsTr("没有找到与「%1」相关的歌曲"),
                                qsTr("没有找到与「%1」相关的歌手"), qsTr("没有找到与「%1」相关的专辑"),
                                qsTr("没有找到与「%1」相关的歌单"), qsTr("没有找到与「%1」相关的播客"),
                                qsTr("没有找到与「%1」相关的声音")][page.tab].arg(page.query)
                }

                // 综合: the best match, when a name is exactly the query.
                BestMatch {
                    visible: page.tab === SearchModel.Overview && page.ready
                             && page.search.overview.bestKind >= 0
                }
                Item {
                    width: parent.width
                    height: 16
                    visible: page.tab === SearchModel.Overview && page.ready
                             && page.search.overview.bestKind >= 0
                }

                // 综合's songs heading; 单曲 and 声音 play what has loaded.
                Item {
                    width: parent.width
                    height: 56
                    visible: page.showSongs
                    GroupTitle {
                        visible: page.tab === SearchModel.Overview
                        text: qsTr("单曲")
                        target: SearchModel.Songs
                    }
                    Button {
                        anchors.verticalCenter: parent.verticalCenter
                        x: page.tab === SearchModel.Overview ? parent.width - width : 0
                        enabled: !page.voices || App.player.canPlayVoices
                        variant: page.tab === SearchModel.Overview ? Md.Tonal : Md.Filled
                        glyph: Symbols.play_arrow
                        text: page.tab === SearchModel.Overview ? qsTr("播放") : qsTr("播放全部")
                        onClicked: page.playSongs(sorted.keys())
                    }
                }

                // Cards: artists, albums, playlists and podcasts.
                CollectionGrid {
                    x: -8
                    width: parent.width + 16
                    visible: page.tabCollections !== null && page.ready
                    model: page.tabCollections ? page.tabCollections.items : null
                    onActivated: (kind, key) => page.shell.openCollection(kind, key)
                }
            }
        }

        footerContent: Column {
            Column {
                x: table.rowX
                width: table.rowWidth
                topPadding: 8
                visible: page.tab === SearchModel.Overview && page.ready
                CardGroup {
                    items: page.search.overview.artists
                    title: qsTr("歌手")
                    target: SearchModel.Artists
                }
                CardGroup {
                    items: page.search.overview.albums
                    title: qsTr("专辑")
                    target: SearchModel.Albums
                }
                CardGroup {
                    items: page.search.overview.playlists
                    title: qsTr("歌单")
                    target: SearchModel.Playlists
                }
            }
            LoadMoreFooter {
                width: parent.width
                model: page.tab === SearchModel.Songs ? page.search.songs
                     : page.tab === SearchModel.Voices ? page.search.voices
                     : page.tabCollections
            }
        }
    }

    // The best match card: opens the page; artists and albums also play.
    component BestMatch: T.AbstractButton {
        id: card

        readonly property SearchOverview overview: page.search.overview
        readonly property bool artist: overview.bestKind === Collection.Artist
        readonly property string typeText: artist ? qsTr("歌手")
                                          : overview.bestKind === Collection.Album ? qsTr("专辑")
                                          : overview.bestKind === Collection.Podcast ? qsTr("播客")
                                          : qsTr("歌单")

        width: Math.min(parent ? parent.width : 0, 560)
        height: 128
        padding: 16
        focusPolicy: Qt.TabFocus
        Accessible.name: [typeText, overview.bestTitle, overview.bestSubtitle]
            .filter(part => part !== "").join(qsTr("，"))
        onClicked: page.shell.openCollection(overview.bestKind, overview.bestKey)

        HoverHandler {
            enabled: card.hoverEnabled
            cursorShape: Qt.PointingHandCursor
        }
        contentItem: Item {
            CoverArt {
                id: bestCover
                width: 96
                height: 96
                anchors.verticalCenter: parent.verticalCenter
                radius: card.artist ? width / 2 : Theme.shape.medium
                glyph: card.artist ? Symbols.person
                     : card.overview.bestKind === Collection.Album ? Symbols.album
                     : Symbols.queue_music
                coverKey: card.overview.bestCoverKey
            }
            Column {
                x: bestCover.width + 16
                width: bestPlay.x - x - 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                // The type in the label's place: the card says what it is.
                Text {
                    text: card.typeText
                    color: Theme.color.primary
                    font: Theme.type.labelLarge.font
                }
                Text {
                    width: parent.width
                    text: card.overview.bestTitle
                    elide: Text.ElideRight
                    color: Theme.color.onSurface
                    font: Theme.type.headlineSmall.font
                }
                Text {
                    width: parent.width
                    text: card.overview.bestSubtitle
                    elide: Text.ElideRight
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.bodyMedium.font
                }
            }
            IconButton {
                id: bestPlay
                x: parent.width - (visible ? width : 0)
                anchors.verticalCenter: parent.verticalCenter
                visible: card.overview.bestKind === Collection.Artist
                         || card.overview.bestKind === Collection.Album
                variant: Md.Filled
                glyph: Symbols.play_arrow
                iconFill: 1
                text: card.artist ? qsTr("播放热门歌曲") : qsTr("播放专辑")
                onClicked: page.playBest()
            }
        }

        background: Rectangle {
            radius: Theme.shape.extraLarge
            color: Theme.color.surfaceContainer
            StateLayer {
                anchors.fill: parent
                radius: parent.radius
                hovered: card.hovered
                pressed: card.pressed
            }
            FocusRing {
                shown: card.visualFocus
                containerRadius: parent.radius
            }
        }
    }

    // The tabs, pinned at the top once the header's have scrolled there.
    Rectangle {
        width: parent.width - table.scrollBarWidth
        height: page.tabsHeight
        visible: page.query !== "" && page.tabsPinned
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
            x: table.rowX
            width: table.rowWidth
            height: parent.height
            Tabs {}
        }
    }

    // Before any search: the trending keywords, as in the search field's list.
    Flickable {
        anchors.fill: parent
        visible: page.query === ""
        contentHeight: hot.implicitHeight + 48
        bottomMargin: PageInsets.bottom
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: hot
            x: Math.max(24, (parent.width - 720) / 2)
            y: 24
            width: Math.min(parent.width - 48, 720)
            spacing: 8

            Text {
                text: qsTr("热门搜索")
                color: Theme.color.onSurface
                font: Theme.type.headlineMediumEmphasized.font
                Accessible.role: Accessible.Heading
            }
            Repeater {
                model: page.search.hotKeywords
                delegate: ListItem {
                    id: hotLine
                    required property int index
                    required property var modelData
                    width: hot.width
                    text: modelData.keyword
                    trailingText: modelData.description
                    leading: Text {
                        width: 24
                        horizontalAlignment: Text.AlignHCenter
                        text: hotLine.index + 1
                        color: hotLine.index < 3 ? Theme.color.primary
                                                 : Theme.color.onSurfaceVariant
                        font: Fonts.tabular(Theme.type.titleSmall.font)
                    }
                    onClicked: {
                        page.shell.searchField.text = modelData.keyword;
                        page.shell.search(modelData.keyword);
                    }
                }
            }
        }
    }
}
