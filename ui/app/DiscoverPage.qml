pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Discover (docs/ui/pages/discover.md): the public catalog in tabs that pin to the top
// once scrolled there: playlists by category (most played, newest or premium), the
// charts, artists by area, kind and initial, and new albums by area (newest, or this
// week's or month's chart). Each tab loads when first shown; changing a filter loads
// again in place. Ctrl+PageUp/PageDown switch tabs; the shell remembers the tab.
Item {
    id: page

    required property AppShell shell

    readonly property int tab: shell.discoverTab
    readonly property int tabCount: 4
    readonly property real tabsHeight: 48
    readonly property real contentX: Math.max(24, (width - 1200) / 2)
    readonly property real contentWidth: Math.min(width - 48, 1200)
    // Filters and sort buttons on one line when there is room.
    readonly property bool wideFilters: contentWidth >= 720
    readonly property LoadableModel tabModel: modelOf(tab)
    readonly property PagedModel pagedModel: tab === 1 ? null : tabModel as PagedModel
    readonly property bool tabsPinned: headerTabs.y - flick.contentY < 0

    // By index: in onTabChanged the tabModel binding may not have followed yet.
    function modelOf(index: int): LoadableModel {
        return [playlists, charts, artists, albums][index];
    }

    function showTab(): void {
        const model = modelOf(tab);
        if (model instanceof DiscoverCardsModel)
            (model as DiscoverCardsModel).ensureLoaded();
        if (tabsPinned)
            Qt.callLater(() => flick.contentY = headerTabs.y);
    }
    onTabChanged: showTab()
    Component.onCompleted: showTab()

    function loadMoreIfNear(): void {
        const model = pagedModel;
        if (model && model.canLoadMore && !model.loadingMore && model.loadMoreError.kind < 0
                && flick.contentY + flick.height > flick.contentHeight - 600)
            model.loadMore();
    }

    PlaylistCategoriesModel {
        id: categories
    }
    DiscoverPlaylistsModel {
        id: playlists
        // Premium only where the category has premium playlists.
        onCategoryChanged: {
            if (order === DiscoverPlaylistsModel.Premium && !page.hasPremium(category))
                order = DiscoverPlaylistsModel.Hot;
        }
    }
    ChartsModel {
        id: charts
    }
    DiscoverArtistsModel {
        id: artists
    }
    DiscoverAlbumsModel {
        id: albums
    }

    // Not from premiumAvailable in onCategoryChanged: that binding may not have followed yet.
    function hasPremium(category: string): bool {
        return category === "" || categories.premium.includes(category);
    }
    readonly property bool premiumAvailable: hasPremium(playlists.category)
    // 全部, the chosen category when it is not a trending one, then the trending ones.
    readonly property list<string> categoryChips: {
        const chips = [qsTr("全部")];
        const chosen = playlists.category;
        if (chosen !== "" && !categories.hot.includes(chosen))
            chips.push(chosen);
        return chips.concat(categories.hot);
    }

    // Plays a chart: its songs load when asked.
    PlaylistPageModel {
        id: chartToPlay
        property bool playWhenReady: false
        onStateChanged: {
            if (playWhenReady && (state === LoadState.Ready || state === LoadState.Partial)) {
                playWhenReady = false;
                if (songs.count > 0)
                    App.player.playSongs(songs.keys(), 0, Playback.FromChart, playlistKey, title);
            }
        }
    }
    function playChart(key: string): void {
        chartToPlay.playWhenReady = true;
        if (chartToPlay.playlistKey === key)
            chartToPlay.refresh();
        chartToPlay.playlistKey = key;
    }

    Shortcut {
        sequences: App.shortcuts.keys.previousTab
        enabled: page.visible
        onActivated: page.shell.discoverTab = (page.tab + page.tabCount - 1) % page.tabCount
    }
    Shortcut {
        sequences: App.shortcuts.keys.nextTab
        enabled: page.visible
        onActivated: page.shell.discoverTab = (page.tab + 1) % page.tabCount
    }

    component Tabs: TabBar {
        id: tabBar
        width: Math.min(parent ? parent.width : 0, 480)
        Component.onCompleted: currentIndex = page.tab
        onCurrentIndexChanged: page.shell.discoverTab = currentIndex
        Connections {
            target: page
            function onTabChanged() {
                tabBar.currentIndex = page.tab;
            }
        }
        TabButton {
            text: qsTr("歌单")
        }
        TabButton {
            text: qsTr("排行榜")
        }
        TabButton {
            text: qsTr("歌手")
        }
        TabButton {
            text: qsTr("新碟")
        }
    }

    // A grid of card placeholders while a tab loads.
    component CardsSkeleton: Grid {
        id: cardsSkeleton
        property real minimumCardWidth: 176
        property bool round: false
        readonly property real cardWidth: width / columns
        width: page.contentWidth
        columns: Math.max(1, Math.floor(width / minimumCardWidth))
        Repeater {
            model: cardsSkeleton.columns * 2
            Item {
                width: cardsSkeleton.cardWidth
                height: width + 56
                Skeleton {
                    x: 8
                    y: 8
                    width: parent.width - 16
                    height: width
                    radius: cardsSkeleton.round ? width / 2 : Theme.shape.medium
                }
                Skeleton {
                    x: 8
                    y: parent.width + 4
                    width: (parent.width - 16) * 0.7
                    height: 14
                }
            }
        }
    }

    // A labeled row of filter chips (the artist filters).
    component FilterRow: Row {
        id: filterRow
        property string label
        property alias model: chips.model
        property alias currentIndex: chips.currentIndex
        signal activated(int index)
        width: page.contentWidth
        height: 40
        spacing: 16
        Text {
            id: rowLabel
            width: 56
            anchors.verticalCenter: parent.verticalCenter
            text: filterRow.label
            color: Theme.color.onSurfaceVariant
            font: Theme.type.labelLarge.font
        }
        ChipGroup {
            id: chips
            width: filterRow.width - rowLabel.width - filterRow.spacing
            anchors.verticalCenter: parent.verticalCenter
            onActivated: index => filterRow.activated(index)
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
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
                text: qsTr("发现")
                color: Theme.color.onSurface
                font: Theme.type.headlineMediumEmphasized.font
                Accessible.role: Accessible.Heading
            }

            Item {
                id: headerTabs
                width: parent.width
                height: page.tabsHeight
                Tabs {
                    opacity: page.tabsPinned ? 0 : 1
                }
            }

            // Playlists: category chips, all categories, and the order.
            Item {
                width: parent.width
                height: page.wideFilters ? 40 : 88
                visible: page.tab === 0

                ChipGroup {
                    id: categoryChips
                    width: (page.wideFilters ? sortGroup.x - 24 : parent.width)
                           - allCategories.width - 8
                    y: 4
                    model: page.categoryChips
                    currentIndex: playlists.category === ""
                                  ? 0 : page.categoryChips.indexOf(playlists.category)
                    onActivated: index => playlists.category = index === 0
                                 ? "" : page.categoryChips[index]
                }
                Button {
                    id: allCategories
                    x: categoryChips.x + categoryChips.shownWidth + 8
                    anchors.verticalCenter: categoryChips.verticalCenter
                    variant: Md.Tonal
                    size: Md.XSmall
                    glyph: Symbols.grid_view
                    text: qsTr("全部分类")
                    enabled: categories.state === LoadState.Ready
                    onClicked: categoryDialog.open()
                }
                ConnectedButtonGroup {
                    id: sortGroup
                    x: page.wideFilters ? parent.width - width : 0
                    y: page.wideFilters ? (40 - height) / 2 : 48 + (40 - height) / 2
                    exclusive: true
                    // A click breaks the checked bindings; the order may also change
                    // from the category (no premium playlists), so follow it here.
                    Connections {
                        target: playlists
                        function onOrderChanged() {
                            [hotButton, newestButton, premiumButton][playlists.order].checked = true;
                        }
                    }
                    Button {
                        id: hotButton
                        variant: Md.Tonal
                        size: Md.XSmall
                        checkable: true
                        checked: playlists.order === DiscoverPlaylistsModel.Hot
                        text: qsTr("最热")
                        onClicked: playlists.order = DiscoverPlaylistsModel.Hot
                    }
                    Button {
                        id: newestButton
                        variant: Md.Tonal
                        size: Md.XSmall
                        checkable: true
                        checked: playlists.order === DiscoverPlaylistsModel.Newest
                        text: qsTr("最新")
                        onClicked: playlists.order = DiscoverPlaylistsModel.Newest
                    }
                    Button {
                        id: premiumButton
                        variant: Md.Tonal
                        size: Md.XSmall
                        checkable: true
                        checked: playlists.order === DiscoverPlaylistsModel.Premium
                        enabled: page.premiumAvailable
                        text: qsTr("精品")
                        onClicked: playlists.order = DiscoverPlaylistsModel.Premium

                        // Disabled buttons get no hover: the reason shows from here.
                        HoverHandler {
                            id: premiumHover
                            enabled: !premiumButton.enabled
                        }
                        ToolTip {
                            visible: premiumHover.hovered
                            text: qsTr("这个分类没有精品歌单")
                        }
                    }
                }
            }

            // Charts: the official ones with their top songs, then the rest.
            Column {
                width: parent.width
                visible: page.tab === 1 && (charts.state === LoadState.Ready
                                            || charts.state === LoadState.Partial)
                spacing: 16

                Text {
                    visible: charts.charts.count > 0
                    text: qsTr("官方榜")
                    color: Theme.color.onSurface
                    font: Theme.type.titleLargeEmphasized.font
                    Accessible.role: Accessible.Heading
                }
                Grid {
                    id: officialCharts
                    readonly property real cardWidth: (width - (columns - 1) * columnSpacing) / columns
                    width: parent.width
                    columns: page.wideFilters ? 2 : 1
                    columnSpacing: 16
                    rowSpacing: 16
                    Repeater {
                        model: charts.charts
                        ChartCard {
                            required property string key
                            required property string title
                            required updateLabel
                            required coverKey
                            required previews
                            width: officialCharts.cardWidth
                            wide: true
                            text: title
                            onClicked: page.shell.openPlaylist(key)
                            onPlayRequested: page.playChart(key)
                        }
                    }
                }
                Text {
                    visible: charts.others.count > 0
                    topPadding: 8
                    text: qsTr("全部榜单")
                    color: Theme.color.onSurface
                    font: Theme.type.titleLargeEmphasized.font
                    Accessible.role: Accessible.Heading
                }
                CollectionGrid {
                    x: -8
                    width: parent.width + 16
                    minimumCardWidth: 176
                    model: charts.others
                    onActivated: (kind, key) => page.shell.openCollection(kind, key)
                }
            }

            // Artists: area, kind, and the initial from a menu.
            Column {
                width: parent.width
                visible: page.tab === 2
                spacing: 4

                FilterRow {
                    label: qsTr("语种")
                    model: [qsTr("全部"), qsTr("华语"), qsTr("欧美"), qsTr("日本"), qsTr("韩国"),
                            qsTr("其他")]
                    currentIndex: artists.area
                    onActivated: index => artists.area = index
                }
                FilterRow {
                    label: qsTr("分类")
                    model: [qsTr("全部"), qsTr("男歌手"), qsTr("女歌手"), qsTr("乐队组合")]
                    currentIndex: artists.kind
                    onActivated: index => artists.kind = index
                }
                Row {
                    height: 40
                    spacing: 16
                    Text {
                        width: 56
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("首字母")
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.labelLarge.font
                    }
                    Chip {
                        id: initialChip
                        anchors.verticalCenter: parent.verticalCenter
                        dropdown: true
                        checked: artists.initial !== ""
                        text: artists.initial !== "" ? artists.initial : qsTr("热门")
                        Accessible.name: qsTr("首字母：%1").arg(text)
                        onClicked: initialMenu.popup(initialChip, 0, initialChip.height)
                    }
                }
            }

            // Albums: the area, and newest or this week's or month's chart.
            Item {
                width: parent.width
                height: page.wideFilters ? 40 : 88
                visible: page.tab === 3

                ChipGroup {
                    width: page.wideFilters ? albumMode.x - 24 : parent.width
                    y: 4
                    model: [qsTr("全部"), qsTr("华语"), qsTr("欧美"), qsTr("韩国"), qsTr("日本")]
                    currentIndex: albums.area
                    onActivated: index => albums.area = index
                }
                ConnectedButtonGroup {
                    id: albumMode
                    x: page.wideFilters ? parent.width - width : 0
                    y: page.wideFilters ? (40 - height) / 2 : 48 + (40 - height) / 2
                    exclusive: true
                    Repeater {
                        model: [qsTr("最新"), qsTr("本周热门"), qsTr("本月热门")]
                        Button {
                            required property int index
                            required property string modelData
                            variant: Md.Tonal
                            size: Md.XSmall
                            checkable: true
                            checked: albums.mode === index
                            text: modelData
                            onClicked: albums.mode = index
                        }
                    }
                }
            }

            TabState {
                model: page.tabModel
                emptyGlyph: [Symbols.queue_music, Symbols.leaderboard, Symbols.person,
                             Symbols.album][page.tab]
                emptyText: [qsTr("这个分类还没有歌单"), qsTr("暂时没有榜单"),
                            qsTr("没有符合条件的歌手"), qsTr("这里还没有新碟")][page.tab]
                skeleton: page.tab === 1 ? null : cardsSkeleton
                Component {
                    id: cardsSkeleton
                    CardsSkeleton {
                        x: -8
                        width: page.contentWidth + 16
                        minimumCardWidth: page.tab === 2 ? 144 : 176
                        round: page.tab === 2
                    }
                }
            }
            CollectionGrid {
                x: -8
                width: parent.width + 16
                visible: page.tab !== 1 && (page.tabModel.state === LoadState.Ready
                                            || page.tabModel.state === LoadState.Partial)
                minimumCardWidth: page.tab === 2 ? 144 : 176
                showPlayCount: page.tab === 0
                model: page.tab === 1 ? null : (page.tabModel as DiscoverCardsModel).items
                onActivated: (kind, key) => page.shell.openCollection(kind, key)
            }
            LoadMoreFooter {
                width: parent.width
                model: page.pagedModel
            }
            // Premium pages stop where the cursor does; say so rather than go quiet.
            Text {
                width: parent.width
                visible: page.tab === 0 && playlists.order === DiscoverPlaylistsModel.Premium
                         && playlists.state === LoadState.Ready && !playlists.canLoadMore
                         && !playlists.loadingMore && playlists.loadMoreError.kind < 0
                topPadding: 8
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("没有更多了")
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }
        }
    }

    // The tabs, pinned at the top once the content's have scrolled there.
    Rectangle {
        width: parent.width - 12
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
        Item {
            x: page.contentX
            width: page.contentWidth
            height: parent.height
            Tabs {}
        }
    }

    // The initial for artists: popular, A–Z, #.
    Menu {
        id: initialMenu
        height: Math.min(implicitHeight, 360)
        Instantiator {
            model: [""].concat("ABCDEFGHIJKLMNOPQRSTUVWXYZ".split(""), ["#"])
            delegate: MenuItem {
                required property string modelData
                checkable: true
                checked: artists.initial === modelData
                text: modelData !== "" ? modelData : qsTr("热门")
                onTriggered: artists.initial = modelData
            }
            onObjectAdded: (index, object) => initialMenu.insertItem(index, object as MenuItem)
            onObjectRemoved: (index, object) => initialMenu.removeItem(object as MenuItem)
        }
    }

    // Every category by group; picking one selects it and closes.
    Dialog {
        id: categoryDialog
        width: Math.min(page.width - 48, 680)
        glyph: Symbols.grid_view
        title: qsTr("全部分类")

        Flickable {
            width: parent.width
            height: Math.min(groups.implicitHeight, page.height - 240)
            contentHeight: groups.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: groups
                // Room for the scroll bar.
                width: parent.width - 16
                spacing: 16

                Chip {
                    filter: true
                    checked: playlists.category === ""
                    text: qsTr("全部")
                    onClicked: {
                        playlists.category = "";
                        categoryDialog.close();
                    }
                }
                Repeater {
                    model: categories.groups
                    Column {
                        id: group
                        required property var modelData
                        width: groups.width
                        spacing: 8
                        Text {
                            text: group.modelData.name
                            color: Theme.color.onSurfaceVariant
                            font: Theme.type.labelLarge.font
                            Accessible.role: Accessible.Heading
                        }
                        Flow {
                            width: parent.width
                            spacing: 8
                            Repeater {
                                model: group.modelData.categories
                                Chip {
                                    required property string modelData
                                    filter: true
                                    checked: playlists.category === modelData
                                    text: modelData
                                    onClicked: {
                                        playlists.category = modelData;
                                        categoryDialog.close();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
