pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Artist page (docs/ui/pages/artist.md) in the shared song page layout: a round
// portrait, name, other names, counts and the brief; play the popular songs, follow.
// Tabs (pinned under the compact bar once scrolled there): songs (paged, most popular
// or newest first), albums (paged cards), the introduction, similar artists. Each tab
// loads when first shown. No in-page find (AR3); Ctrl+PageUp/PageDown switch tabs.
SongCollectionPage {
    id: page
    origin: Playback.FromArtist
    originId: pageModel.artistKey

    property alias artistKey: pageModel.artistKey
    shareKind: Share.Artist
    shareKey: pageModel.artistKey
    // 0 songs, 1 albums, 2 introduction, 3 similar artists.
    property int currentTab: 0
    readonly property int tabCount: 4

    model: pageModel
    songs: pageModel.songs.songs
    title: pageModel.name
    coverKey: pageModel.avatarKey
    cover: CoverArt {
        radius: width / 2
        glyph: Symbols.person
        coverKey: pageModel.avatarKey
    }
    roundCover: true
    sortable: false
    findable: false
    playText: qsTr("播放热门歌曲")
    playSource: pageModel.topSongs
    actionSkeletons: [148, 96]
    showSongs: currentTab === 0
    showBody: currentTab !== 0 || pageModel.songs.state !== LoadState.Ready
    footerContent: LoadMoreFooter {
        model: page.currentTab === 0 ? pageModel.songs
             : page.currentTab === 1 ? pageModel.albums : null
    }

    function toggleFollowed(): void {
        if (!App.session.signedIn)
            page.shell.openLogin();
        else
            pageModel.followed = !pageModel.followed;
    }

    // The tab's content loads when first shown. With the tabs pinned, the new tab
    // starts at its top, under them.
    function showTab(): void {
        if (currentTab === 0)
            pageModel.songs.ensureLoaded();
        else if (currentTab === 1)
            pageModel.albums.ensureLoaded();
        else if (currentTab === 2)
            pageModel.introduction.ensureLoaded();
        else
            pageModel.similar.ensureLoaded();
        if (tabsPinned && headerTabs)
            Qt.callLater(() => {
                const view = page.table.view;
                view.contentY = view.originY + page.headerTabs.y - 56;
            });
    }
    onCurrentTabChanged: showTab()

    // The next page once the end is near: songs by row, album cards by scroll.
    function loadMoreIfNear(): void {
        const view = page.table.view;
        if (currentTab === 0) {
            const songs = pageModel.songs;
            if (songs.canLoadMore && !songs.loadingMore && songs.loadMoreError.kind < 0
                    && page.table.lastVisibleRow >= songs.songs.count - 20)
                songs.loadMore();
        } else if (currentTab === 1) {
            const albums = pageModel.albums;
            if (albums.canLoadMore && !albums.loadingMore && albums.loadMoreError.kind < 0
                    && view.contentY + view.height > view.originY + view.contentHeight - 600)
                albums.loadMore();
        }
    }
    Connections {
        target: page.table.view
        function onContentYChanged() {
            page.loadMoreIfNear();
        }
        function onContentHeightChanged() {
            Qt.callLater(page.loadMoreIfNear);
        }
    }

    Shortcut {
        sequences: App.shortcuts.keys.previousTab
        enabled: page.visible
        onActivated: page.currentTab = (page.currentTab + page.tabCount - 1) % page.tabCount
    }
    Shortcut {
        sequences: App.shortcuts.keys.nextTab
        enabled: page.visible
        onActivated: page.currentTab = (page.currentTab + 1) % page.tabCount
    }

    ArtistPageModel {
        id: pageModel
        onLibraryChanged: page.shell.refreshLibrary()
        onNotice: message => page.shell.notify(message)
        onStateChanged: {
            if (pageModel.state === LoadState.Ready)
                page.showTab();
        }
    }

    infoContent: Column {
        spacing: 8

        KindLabel {
            text: qsTr("歌手")
            color: page.infoAccentColor
            chip: page.twoPane
        }
        Column {
            width: parent.width
            Text {
                width: parent.width
                text: pageModel.name
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: page.infoColor
                font: page.titleFont
                Accessible.role: Accessible.Heading
            }
            Text {
                width: parent.width
                visible: pageModel.aliasText !== ""
                text: pageModel.aliasText
                elide: Text.ElideRight
                color: page.infoVariantColor
                font: Theme.type.bodyMedium.font
            }
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: pageModel.countText
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
        Brief {
            width: parent.width
            visible: page.wide && pageModel.description !== ""
        }
    }

    extraContent: Brief {}
    showExtra: !page.wide && pageModel.description !== ""

    // The brief: one line; "更多" goes to the introduction tab.
    component Brief: Column {
        Text {
            id: brief
            width: parent.width
            text: pageModel.description
            maximumLineCount: page.twoPane ? 4 : 1
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
        Button {
            visible: brief.truncated
            variant: Md.Text
            size: Md.XSmall
            text: qsTr("更多")
            onClicked: page.currentTab = 2
        }
    }

    actionsContent: Flow {
        spacing: 8
        PageAction {
            iconOnly: page.twoPane
            size: page.paneActionSize
            glyph: pageModel.followed ? Symbols.how_to_reg : Symbols.person_add
            text: pageModel.followed ? qsTr("已关注") : qsTr("关注")
            onClicked: page.toggleFollowed()
        }
        // A musician's own account (docs/ui/pages/user.md §4).
        PageAction {
            visible: pageModel.userKey !== ""
            iconOnly: page.twoPane
            size: page.paneActionSize
            variant: Md.Text
            glyph: Symbols.person
            text: qsTr("用户主页")
            onClicked: page.shell.openUser(pageModel.userKey)
        }
    }

    tabsContent: Item {
        TabBar {
            id: tabBar
            width: Math.min(parent.width - (order.visible ? order.width + 16 : 0), 480)
            anchors.verticalCenter: parent.verticalCenter
            Component.onCompleted: currentIndex = page.currentTab
            onCurrentIndexChanged: page.currentTab = currentIndex
            Connections {
                target: page
                function onCurrentTabChanged() {
                    tabBar.currentIndex = page.currentTab;
                }
            }
            TabButton {
                text: qsTr("歌曲")
            }
            TabButton {
                text: pageModel.albumCount > 0 ? qsTr("专辑 %1").arg(pageModel.albumCount)
                                               : qsTr("专辑")
            }
            TabButton {
                text: qsTr("详情")
            }
            TabButton {
                text: qsTr("相似歌手")
            }
        }
        // Songs: most popular or newest first (§3.1).
        ConnectedButtonGroup {
            id: order
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: page.currentTab === 0
            exclusive: true
            Button {
                size: Md.XSmall
                variant: Md.Tonal
                checkable: true
                checked: !pageModel.songs.byTime
                text: qsTr("最热")
                onClicked: pageModel.songs.byTime = false
            }
            Button {
                size: Md.XSmall
                variant: Md.Tonal
                checkable: true
                checked: pageModel.songs.byTime
                text: qsTr("最新")
                onClicked: pageModel.songs.byTime = true
            }
        }
    }

    bodyContent: Item {
        implicitHeight: content.implicitHeight

        Column {
            id: content
            width: parent.width
            topPadding: 8

            TabState {
                visible: page.currentTab === 0 && height > 0
                model: pageModel.songs
                emptyGlyph: Symbols.music_note
                emptyText: qsTr("暂时没有这位歌手的歌")
            }

            // Albums.
            TabState {
                visible: page.currentTab === 1 && height > 0
                model: pageModel.albums
                emptyGlyph: Symbols.album
                emptyText: qsTr("暂时没有这位歌手的专辑")
            }
            CollectionGrid {
                width: parent.width
                visible: page.currentTab === 1
                model: pageModel.albums.items
                onActivated: (kind, key) => page.shell.openCollection(kind, key)
            }

            // Introduction: selectable plain text, at a readable width.
            TabState {
                visible: page.currentTab === 2 && height > 0
                model: pageModel.introduction
                emptyGlyph: Symbols.person
                emptyText: qsTr("暂时没有这位歌手的介绍")
            }
            Column {
                width: Math.min(parent.width, 720)
                visible: page.currentTab === 2
                         && pageModel.introduction.state === LoadState.Ready
                spacing: 24
                ReadingText {
                    visible: text !== ""
                    text: pageModel.introduction.brief
                }
                Repeater {
                    model: pageModel.introduction.sections
                    delegate: Column {
                        id: section
                        required property var modelData
                        width: parent.width
                        spacing: 8
                        Text {
                            width: parent.width
                            visible: text !== ""
                            text: section.modelData.title
                            wrapMode: Text.Wrap
                            color: Theme.color.onSurface
                            font: Theme.type.titleMedium.font
                            Accessible.role: Accessible.Heading
                        }
                        ReadingText {
                            text: section.modelData.text
                        }
                    }
                }
            }

            // Similar artists.
            TabState {
                visible: page.currentTab === 3 && height > 0
                model: pageModel.similar
                emptyGlyph: Symbols.person
                emptyText: qsTr("暂时没有相似的歌手")
                signedOutText: qsTr("登录后可以看到相似的歌手")
                onSignInRequested: page.shell.openLogin()
            }
            CollectionGrid {
                width: parent.width
                visible: page.currentTab === 3
                model: pageModel.similar.items
                onActivated: (kind, key) => page.shell.openCollection(kind, key)
            }
        }
    }

    // Body text that can be selected and copied.
    component ReadingText: SelectableText {
        width: parent ? parent.width : 0
        color: Theme.color.onSurfaceVariant
    }
}
