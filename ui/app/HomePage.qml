pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The home page (docs/ui/pages/home.md, redesigned in docs/ui/redesign-md3e.md §6):
// containers of different sizes and roles on top ("today" as the hero; personal FM,
// the liked songs and this week's listening beside it), then the songs played lately
// beside today's playlists, then quiet shelves: recommended playlists, charts, new
// albums and popular artists. Guests get the sign-in invitation instead of the private
// parts. Every section loads and fails on its own.
Item {
    id: page

    required property AppShell shell

    readonly property bool signedIn: App.session.signedIn
    // Content column: 32 from the panel's sides, at most 1280 wide, centered.
    readonly property real columnWidth: Math.min(width - 64, 1280)
    readonly property bool wide: columnWidth >= 792
    // Room for two things side by side: the hero beside a column of tiles, the songs
    // played lately beside today's playlists. Narrower, they stack.
    readonly property bool bento: signedIn && columnWidth >= 1000
    readonly property real sideWidth: Math.round(Math.max(360, Math.min(436, columnWidth * 0.34)))
    readonly property real gap: 12

    // Everything the account can see failed: one page-level message instead.
    readonly property bool allFailed: {
        const failed = m => m.state === LoadState.Error || m.state === LoadState.Offline;
        const common = failed(recommended) && failed(charts) && failed(newAlbums)
                       && failed(artists);
        return signedIn ? common && failed(daily) && failed(recent) && failed(dailyPlaylists)
                        : common;
    }

    function refresh() {
        for (const m of [daily, recent, dailyPlaylists, recommended, charts, newAlbums, artists])
            m.retry();
    }

    DailySongsPageModel {
        id: daily
    }
    RecentSongsModel {
        id: recent
        limit: 6
    }
    ShelfModel {
        id: dailyPlaylists
        shelf: Feed.DailyPlaylists
    }
    ShelfModel {
        id: recommended
        shelf: Feed.RecommendedPlaylists
    }
    ChartsModel {
        id: charts
    }
    ShelfModel {
        id: newAlbums
        shelf: Feed.NewAlbums
    }
    ShelfModel {
        id: artists
        shelf: Feed.PopularArtists
    }

    // Plays a playlist from a card: its songs load when asked.
    PlaylistPageModel {
        id: playlistPlayer
        property bool playWhenReady: false
        onStateChanged: {
            if (playWhenReady && (state === LoadState.Ready || state === LoadState.Partial)) {
                playWhenReady = false;
                if (songs.count > 0)
                    App.player.playSongs(songs.keys(), 0, Playback.FromPlaylist, playlistKey, title);
            }
        }
    }
    function playPlaylist(key: string): void {
        playlistPlayer.playWhenReady = true;
        if (playlistPlayer.playlistKey === key)
            playlistPlayer.refresh();
        playlistPlayer.playlistKey = key;
    }

    Shortcut {
        sequences: App.shortcuts.keys.refresh
        enabled: page.visible
        onActivated: page.refresh()
    }

    component Placeholder: Column {
        id: rowsPlaceholder
        spacing: 8
        Repeater {
            model: 3
            Skeleton {
                width: rowsPlaceholder.width
                height: 64
                radius: Theme.shape.large
            }
        }
    }
    component CardsPlaceholder: Row {
        id: cardsPlaceholder
        spacing: 16
        Repeater {
            model: Math.max(1, Math.floor(cardsPlaceholder.width / 176))
            Skeleton {
                width: 160
                height: 200
                radius: Theme.shape.large
            }
        }
    }

    // One row of cards, as many as fit.
    component Cards: CollectionGrid {
        property real room: page.columnWidth
        width: room + 16
        x: -8
        minimumCardWidth: 200
        maximumRows: 1
        onActivated: (kind, key) => page.shell.openCollection(kind, key)
    }

    // The first `limit` songs in one or two segmented columns; a row plays the section's
    // songs.
    component SongColumns: Row {
        id: songColumns
        required property SongListModel songs
        property int columns: 1
        property int limit: 6
        readonly property int shown: Math.min(songs.count, limit)
        readonly property int perColumn: Math.ceil(shown / columns)
        spacing: 16
        Repeater {
            model: songColumns.columns
            Column {
                id: songColumn
                required property int index
                readonly property int firstRow: index * songColumns.perColumn
                width: (songColumns.width - (songColumns.columns - 1) * songColumns.spacing)
                       / songColumns.columns
                spacing: 2
                Repeater {
                    model: songColumns.songs
                    ListItem {
                        id: row
                        required property int index
                        required property string title
                        required property string artistText
                        required property string coverKey
                        required property string durationText
                        required property bool current
                        readonly property int lastRow: Math.min(songColumns.shown,
                                                             songColumn.firstRow
                                                             + songColumns.perColumn) - 1
                        width: songColumn.width
                        visible: index >= songColumn.firstRow && index <= lastRow
                        segmented: true
                        first: index === songColumn.firstRow
                        last: index === row.lastRow
                        containerColor: Theme.color.surfaceContainer
                        highlighted: current
                        text: title
                        supportingText: artistText
                        trailingText: durationText
                        onClicked: App.player.playSongs(songColumns.songs.keys(), index)
                        leading: Component {
                            CoverArt {
                                width: 48
                                height: 48
                                coverKey: row.coverKey
                            }
                        }
                    }
                }
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        visible: !page.allFailed
        contentHeight: column.height + 48
        bottomMargin: PageInsets.bottom
        // Overshoots and springs back at the ends.
        boundsBehavior: Flickable.DragAndOvershootBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: column
            x: (page.width - width) / 2
            y: 20
            width: page.columnWidth
            spacing: 40

            // The first screen: the hero row and, close under it, the recent songs and
            // today's playlists (redesign-md3e.md §6.2).
            Column {
                width: column.width
                spacing: page.bento ? page.gap : 40

                // The hero and the tiles. Wide enough: "today" beside a column (FM over the
                // liked songs and this week); less wide: the three in a row under it; narrow:
                // FM as a strip, then the two tiles.
                Item {
                    id: topRow
                    readonly property real tileWidth: page.bento ? (page.sideWidth - page.gap) / 2
                                                    : page.wide ? 212 : (width - page.gap) / 2
                    width: column.width
                    height: page.bento || !page.signedIn ? today.height : liked.y + liked.height

                    TodayCard {
                        id: today
                        shell: page.shell
                        daily: daily
                        width: page.bento ? parent.width - page.sideWidth - page.gap : parent.width
                        height: implicitHeight
                    }
                    FmCard {
                        id: fm
                        shell: page.shell
                        visible: page.signedIn
                        strip: !page.wide
                        x: page.bento ? today.width + page.gap : 0
                        y: page.bento ? 0 : today.height + page.gap
                        width: page.bento ? page.sideWidth
                             : page.wide ? parent.width - 2 * (topRow.tileWidth + page.gap) : parent.width
                        height: page.bento ? (today.height - page.gap) / 2 : page.wide ? 180 : 72
                    }
                    LikedTile {
                        id: liked
                        shell: page.shell
                        visible: page.signedIn
                        x: page.bento ? fm.x : page.wide ? fm.width + page.gap : 0
                        y: page.bento ? fm.height + page.gap
                         : page.wide ? fm.y : fm.y + fm.height + page.gap
                        width: topRow.tileWidth
                        height: page.bento ? fm.height : 180
                    }
                    WeekTile {
                        shell: page.shell
                        visible: page.signedIn
                        x: liked.x + liked.width + page.gap
                        y: liked.y
                        width: topRow.tileWidth
                        height: liked.height
                    }
                }

                // The songs played lately and today's playlists: side by side when there is
                // room, one under the other otherwise.
                Item {
                    id: pair
                    readonly property real leftWidth: page.bento ? Math.round(width * 0.41) : width
                    readonly property bool hasRecent: page.signedIn && recentSection.wanted
                    readonly property bool hasDaily: page.signedIn && dailySection.wanted
                    readonly property real recentHeight: hasRecent ? recentSection.height : 0
                    readonly property real dailyHeight: hasDaily ? dailySection.height : 0
                    width: column.width
                    visible: hasRecent || hasDaily
                    height: page.bento ? Math.max(recentHeight, dailyHeight)
                                       : recentHeight + dailyHeight + (hasRecent && hasDaily ? 40 : 0)

                    HomeSection {
                        id: recentSection
                        width: pair.leftWidth
                        visible: pair.hasRecent
                        title: qsTr("最近听过")
                        showsAll: true
                        model: recent
                        skeleton: Placeholder {}
                        onAllRequested: page.shell.openRecent()

                        SongColumns {
                            width: recentSection.width
                            songs: recent.songs
                            columns: !page.bento && page.wide ? 2 : 1
                            limit: page.bento ? 4 : 6
                        }
                    }

                    HomeSection {
                        id: dailySection
                        x: page.bento && pair.hasRecent ? pair.leftWidth + page.gap : 0
                        y: page.bento || !pair.hasRecent ? 0 : pair.recentHeight + 40
                        width: pair.width - x
                        visible: pair.hasDaily
                        title: qsTr("每日推荐歌单")
                        model: dailyPlaylists
                        skeleton: CardsPlaceholder {}
                        // Pages the carousel, while there is more than it shows.
                        trailing: Row {
                            visible: dailyCarousel.end > 0
                            spacing: 4
                            IconButton {
                                enabled: dailyCarousel.canGoBack
                                variant: Md.Tonal
                                glyph: Symbols.chevron_left
                                text: qsTr("往前")
                                onClicked: dailyCarousel.back()
                            }
                            IconButton {
                                enabled: dailyCarousel.canGoForward
                                variant: Md.Tonal
                                glyph: Symbols.chevron_right
                                text: qsTr("往后")
                                onClicked: dailyCarousel.forward()
                            }
                        }

                        Carousel {
                            id: dailyCarousel
                            width: dailySection.width
                            height: Math.min(246, largeWidth)
                            preferredItemWidth: 246
                            model: dailyPlaylists.items
                            onActivated: index => (itemAt(index) as CarouselCard).trigger()
                            delegate: CarouselCard {
                                required property int index
                                required property string key
                                required property string title
                                required kind
                                required coverKey
                                required subtitle
                                text: title
                                playable: kind === Collection.Playlist
                                onPlayRequested: page.playPlaylist(key)
                                largeWidth: dailyCarousel.largeWidth
                                current: dailyCarousel.currentShown && dailyCarousel.currentIndex === index
                                onClicked: {
                                    dailyCarousel.currentIndex = index;
                                    page.shell.openCollection(kind, key);
                                }
                            }
                        }
                    }
                }
            }

            HomeSection {
                width: column.width
                title: qsTr("推荐歌单")
                showsAll: true
                model: recommended
                skeleton: CardsPlaceholder {}
                onAllRequested: page.shell.openDiscover(0)

                Cards {
                    model: recommended.items
                }
            }

            HomeSection {
                width: column.width
                title: qsTr("榜单")
                showsAll: true
                model: charts
                skeleton: CardsPlaceholder {}
                onAllRequested: page.shell.openDiscover(1)

                Flow {
                    id: chartFlow
                    readonly property int columns: Math.max(1, Math.floor((width + spacing) / 276))
                    width: column.width
                    spacing: 16
                    Repeater {
                        model: charts.charts
                        ChartCard {
                            required property int index
                            required property string key
                            required property string title
                            required updateLabel
                            required coverKey
                            required previews
                            width: (chartFlow.width - (chartFlow.columns - 1) * chartFlow.spacing)
                                   / chartFlow.columns
                            text: title
                            badge: index
                            onClicked: page.shell.openPlaylist(key)
                        }
                    }
                }
            }

            HomeSection {
                width: column.width
                title: qsTr("新碟上架")
                showsAll: true
                model: newAlbums
                skeleton: CardsPlaceholder {}
                onAllRequested: page.shell.openDiscover(3)

                Cards {
                    model: newAlbums.items
                }
            }

            HomeSection {
                width: column.width
                title: qsTr("热门歌手")
                showsAll: true
                model: artists
                skeleton: CardsPlaceholder {}
                onAllRequested: page.shell.openDiscover(2)

                Cards {
                    minimumCardWidth: 144
                    model: artists.items
                }
            }
        }
    }

    // Everything failed: said by why. The backend not started, the network (any section
    // offline or a network failure), or the service.
    StateMessage {
        readonly property bool noBackend: App.startupProblem !== ""
        readonly property bool network: {
            for (const m of [daily, recent, dailyPlaylists, recommended, charts, newAlbums, artists])
                if (m.state === LoadState.Offline || m.error.kind === Failure.Network
                        || m.error.kind === Failure.Timeout)
                    return true;
            return false;
        }
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 480)
        visible: page.allFailed
        glyph: noBackend ? Symbols.error : network ? Symbols.wifi_off : Symbols.cloud_off
        tone: Md.Neutral
        title: noBackend ? qsTr("Cirrus 没能启动") : network ? qsTr("连不上网易云音乐")
                                                             : qsTr("首页没有加载出来")
        message: noBackend ? App.startupProblem : network ? qsTr("检查网络后重试。")
                                                          : qsTr("网易云音乐暂时没有回应，稍后重试。")
        actionText: noBackend ? "" : qsTr("重试")
        actionGlyph: Symbols.refresh
        onActionTriggered: page.refresh()
    }
}
