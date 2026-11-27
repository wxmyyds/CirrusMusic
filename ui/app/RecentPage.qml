pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Recently played (docs/ui/pages/library.md §3.2), the service's record: songs (at
// most 100) in the shared song page layout, and playlists, albums, podcasts and
// voices in tabs pinned under the compact bar. The order is the time played: no
// sorting or find. Ctrl+PageUp/PageDown switch tabs.
SongCollectionPage {
    id: page
    origin: Playback.FromRecent

    // 0 songs, 1 playlists, 2 albums, 3 podcasts, 4 voices.
    property int currentTab: 0
    readonly property int tabCount: 5
    readonly property LoadableModel tabModel: modelOf(currentTab)
    readonly property bool tabReady: tabModel.state === LoadState.Ready
                                     || tabModel.state === LoadState.Partial

    model: pageModel
    songs: currentTab === 4 ? pageModel.voices.songs : pageModel.songs
    title: qsTr("最近播放")
    cover: GlyphTile {
        glyph: Symbols.history
    }
    sortable: false
    findable: false
    voices: currentTab === 4
    // The button plays the songs, whichever tab shows.
    playsVoices: false
    playSource: pageModel.songs
    showLiked: !voices
    albumColumn: !voices
    showAlbumAction: !voices
    actionSkeletons: [116]
    emptyGlyph: Symbols.history
    emptyText: qsTr("还没有播放记录")
    signedOutText: qsTr("登录后可以看到网易云记录的最近播放")
    showSongs: currentTab === 0 || (currentTab === 4 && tabReady)
    showBody: currentTab !== 0 && !(currentTab === 4 && tabReady)

    // By index: in onCurrentTabChanged the tabModel binding may not have followed yet.
    function modelOf(index: int): LoadableModel {
        return [pageModel, pageModel.playlists, pageModel.albums, pageModel.podcasts,
                pageModel.voices][index];
    }

    function showTab(): void {
        if (currentTab === 4)
            pageModel.voices.ensureLoaded();
        else if (currentTab > 0)
            (modelOf(currentTab) as RecentCollectionsModel).ensureLoaded();
        if (tabsPinned && headerTabs)
            Qt.callLater(() => {
                const view = page.table.view;
                view.contentY = view.originY + page.headerTabs.y - 56;
            });
    }
    onCurrentTabChanged: showTab()

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

    RecentPageModel {
        id: pageModel
    }

    infoContent: Column {
        spacing: 8

        KindLabel {
            text: qsTr("音乐库")
            color: page.infoAccentColor
            chip: page.twoPane
        }
        Text {
            width: parent.width
            text: qsTr("最近播放")
            elide: Text.ElideRight
            color: page.infoColor
            font: page.titleFont
            Accessible.role: Accessible.Heading
        }
        Text {
            width: parent.width
            text: qsTr("网易云记录的最近播放，所有设备都算")
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: pageModel.countText
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
    }

    tabsContent: Item {
        TabBar {
            id: tabBar
            width: Math.min(parent.width, 560)
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
                text: qsTr("歌单")
            }
            TabButton {
                text: qsTr("专辑")
            }
            TabButton {
                text: qsTr("播客")
            }
            TabButton {
                text: qsTr("声音")
            }
        }
    }

    bodyContent: Column {
        topPadding: 8

        TabState {
            visible: page.currentTab > 0 && height > 0
            model: page.tabModel
            emptyGlyph: [Symbols.history, Symbols.queue_music, Symbols.album, Symbols.podcasts,
                         Symbols.podcasts][page.currentTab]
            emptyText: [qsTr("还没有播放记录"), qsTr("最近没有播放过歌单"),
                        qsTr("最近没有播放过专辑"), qsTr("最近没有听过播客"),
                        qsTr("最近没有听过声音")][page.currentTab]
        }
        CollectionGrid {
            x: -8
            width: parent.width + 16
            visible: page.currentTab >= 1 && page.currentTab <= 3 && page.tabReady
            model: page.currentTab === 1 ? pageModel.playlists.items
                 : page.currentTab === 2 ? pageModel.albums.items
                 : page.currentTab === 3 ? pageModel.podcasts.items : null
            onActivated: (kind, key) => page.shell.openCollection(kind, key)
        }
    }
}
