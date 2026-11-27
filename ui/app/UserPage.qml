pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// User page (docs/ui/pages/user.md) in the shared song page layout: a round avatar,
// the nickname with the level, the signature and the counts (each opens its tab); a
// musician's artist page. Tabs (pinned under the compact bar once scrolled there):
// playlists (made and saved), the listening ranking (the song table), follows, fans.
// Each tab loads when first shown; Ctrl+PageUp/PageDown switch tabs.
SongCollectionPage {
    id: page
    // Only the listening ranking plays songs here.
    origin: Playback.FromRanking
    originId: pageModel.userKey

    property alias userKey: pageModel.userKey
    shareKind: Share.User
    shareKey: pageModel.userKey
    // 0 playlists, 1 listening ranking, 2 follows, 3 fans.
    property int currentTab: 0
    readonly property int tabCount: 4

    model: pageModel
    songs: pageModel.ranking.songs
    title: pageModel.nickname
    coverKey: pageModel.avatarKey
    cover: CoverArt {
        radius: width / 2
        glyph: Symbols.person
        coverKey: pageModel.avatarKey
    }
    roundCover: true
    showPlay: false
    showActions: !pageModel.own || pageModel.artistKey !== ""
    sortable: false
    findable: false
    ranked: true
    actionSkeletons: [104]
    // Only with songs: an empty or hidden ranking is the tab's state below.
    showSongs: currentTab === 1 && pageModel.ranking.songs.count > 0
    showBody: currentTab !== 1 || pageModel.ranking.state !== LoadState.Ready
    footerContent: LoadMoreFooter {
        model: page.currentTab === 2 ? pageModel.follows
             : page.currentTab === 3 ? pageModel.fans : null
    }

    // "TA" for others, nothing for one's own page.
    function their(other: string, own: string): string {
        return pageModel.own ? own : other;
    }

    function toggleFollowed(): void {
        if (!App.session.signedIn)
            page.shell.openLogin();
        else
            pageModel.followed = !pageModel.followed;
    }

    function countLabel(label: string, count: real): string {
        return count >= 0 ? qsTr("%1 %2").arg(label).arg(App.format.count(count)) : label;
    }

    // The tab's content loads when first shown. With the tabs pinned, the new tab
    // starts at its top, under them.
    function showTab(): void {
        if (currentTab === 0)
            pageModel.playlists.ensureLoaded();
        else if (currentTab === 1)
            pageModel.ranking.ensureLoaded();
        else if (currentTab === 2)
            pageModel.follows.ensureLoaded();
        else
            pageModel.fans.ensureLoaded();
        if (tabsPinned && headerTabs)
            Qt.callLater(() => {
                const view = page.table.view;
                view.contentY = view.originY + page.headerTabs.y - 56;
            });
    }
    onCurrentTabChanged: showTab()

    // The next page of people once the end is near.
    function loadMoreIfNear(): void {
        const view = page.table.view;
        const people = currentTab === 2 ? pageModel.follows
                     : currentTab === 3 ? pageModel.fans : null;
        if (people && people.canLoadMore && !people.loadingMore && people.loadMoreError.kind < 0
                && view.contentY + view.height > view.originY + view.contentHeight - 600)
            people.loadMore();
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

    UserPageModel {
        id: pageModel
        onNotice: message => page.shell.notify(message)
        onStateChanged: {
            if (pageModel.state === LoadState.Ready)
                page.showTab();
        }
    }

    Dialog {
        id: signatureDialog
        reading: true
        title: pageModel.nickname
        supportingText: pageModel.signature
    }

    infoContent: Column {
        spacing: 8

        Row {
            width: parent.width
            spacing: 12
            Text {
                id: nickname
                width: Math.min(implicitWidth, parent.width - (level.visible ? level.width + 12 : 0))
                anchors.verticalCenter: parent.verticalCenter
                text: pageModel.nickname
                elide: Text.ElideRight
                color: page.infoColor
                font: page.titleFont
                Accessible.role: Accessible.Heading
            }
            // The level, as a small label.
            Rectangle {
                id: level
                visible: pageModel.level >= 0
                anchors.verticalCenter: parent.verticalCenter
                width: levelText.implicitWidth + 12
                height: 20
                radius: height / 2
                color: ColorScope.scheme.secondaryContainer
                Text {
                    id: levelText
                    anchors.centerIn: parent
                    text: qsTr("Lv.%1").arg(pageModel.level)
                    color: ColorScope.scheme.onSecondaryContainer
                    font: Theme.type.labelSmall.font
                }
            }
        }
        Signature {
            width: parent.width
            visible: page.wide && pageModel.signature !== ""
        }
        // The counts; each opens its tab.
        Row {
            spacing: 4
            CountButton {
                label: qsTr("关注")
                count: pageModel.followCount
                tab: 2
            }
            CountButton {
                label: qsTr("粉丝")
                count: pageModel.fanCount
                tab: 3
            }
            CountButton {
                label: qsTr("歌单")
                count: pageModel.playlistCount
                tab: 0
            }
        }
    }

    extraContent: Signature {}
    showExtra: !page.wide && pageModel.signature !== ""

    // The signature: one line; "更多" opens it in a reading dialog.
    component Signature: Column {
        Text {
            id: signature
            width: parent.width
            text: pageModel.signature
            maximumLineCount: page.twoPane ? 4 : 1
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
        Button {
            visible: signature.truncated
            variant: Md.Text
            size: Md.XSmall
            text: qsTr("更多")
            onClicked: signatureDialog.open()
        }
    }

    component CountButton: Button {
        id: countButton
        required property string label
        required property real count
        required property int tab
        visible: count >= 0
        variant: Md.Text
        size: Md.XSmall
        text: page.countLabel(label, count)
        onClicked: page.currentTab = tab
    }

    actionsContent: Flow {
        spacing: 8
        PageAction {
            visible: !pageModel.own && pageModel.state === LoadState.Ready
            iconOnly: page.twoPane
            size: page.paneActionSize
            glyph: pageModel.followed ? Symbols.how_to_reg : Symbols.person_add
            text: pageModel.followed ? qsTr("已关注") : qsTr("关注")
            onClicked: page.toggleFollowed()
        }
        PageAction {
            visible: pageModel.artistKey !== ""
            iconOnly: page.twoPane
            size: page.paneActionSize
            variant: Md.Text
            glyph: Symbols.mic
            text: qsTr("歌手主页")
            onClicked: page.shell.openArtist(pageModel.artistKey)
        }
    }

    compactActionsContent: FollowButton {
        size: Md.Small
    }

    // Follow or unfollow; not on one's own page. Guests are asked to sign in.
    component FollowButton: Button {
        visible: !pageModel.own && pageModel.state === LoadState.Ready
        variant: Md.Tonal
        glyph: pageModel.followed ? Symbols.how_to_reg : Symbols.person_add
        text: pageModel.followed ? qsTr("已关注") : qsTr("关注")
        onClicked: page.toggleFollowed()
    }

    tabsContent: Item {
        TabBar {
            id: tabBar
            width: Math.min(parent.width - (period.visible ? period.width + 16 : 0), 480)
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
                text: qsTr("歌单")
            }
            TabButton {
                text: qsTr("听歌排行")
            }
            TabButton {
                text: page.countLabel(qsTr("关注"), pageModel.followCount)
            }
            TabButton {
                text: page.countLabel(qsTr("粉丝"), pageModel.fanCount)
            }
        }
        // The ranking: the last week or all time.
        ConnectedButtonGroup {
            id: period
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: page.currentTab === 1 && !pageModel.ranking.hidden
            exclusive: true
            Button {
                size: Md.XSmall
                variant: Md.Tonal
                checkable: true
                checked: !pageModel.ranking.allTime
                text: qsTr("最近一周")
                onClicked: pageModel.ranking.allTime = false
            }
            Button {
                size: Md.XSmall
                variant: Md.Tonal
                checkable: true
                checked: pageModel.ranking.allTime
                text: qsTr("所有时间")
                onClicked: pageModel.ranking.allTime = true
            }
        }
    }

    bodyContent: Item {
        implicitHeight: content.implicitHeight

        Column {
            id: content
            width: parent.width
            topPadding: 8
            spacing: 8

            // Playlists: made, then saved.
            TabState {
                visible: page.currentTab === 0 && height > 0
                model: pageModel.playlists
                emptyGlyph: Symbols.queue_music
                emptyText: page.their(qsTr("TA 还没有歌单"), qsTr("还没有歌单"))
            }
            PlaylistSection {
                title: qsTr("创建的歌单")
                items: pageModel.playlists.created
            }
            PlaylistSection {
                title: qsTr("收藏的歌单")
                items: pageModel.playlists.saved
            }

            TabState {
                visible: page.currentTab === 1 && height > 0
                model: pageModel.ranking
                emptyGlyph: pageModel.ranking.hidden ? Symbols.lock : Symbols.leaderboard
                emptyText: pageModel.ranking.hidden ? qsTr("TA 没有公开听歌排行")
                         : pageModel.ranking.allTime ? qsTr("还没有听歌记录")
                         : qsTr("这一周还没有听歌记录")
            }

            TabState {
                visible: page.currentTab === 2 && height > 0
                model: pageModel.follows
                emptyGlyph: pageModel.follows.hidden ? Symbols.lock : Symbols.person
                emptyText: pageModel.follows.hidden ? qsTr("TA 没有公开关注列表")
                         : page.their(qsTr("TA 还没有关注别人"), qsTr("还没有关注别人"))
            }
            People {
                visible: page.currentTab === 2
                people: pageModel.follows
            }

            TabState {
                visible: page.currentTab === 3 && height > 0
                model: pageModel.fans
                emptyGlyph: pageModel.fans.hidden ? Symbols.lock : Symbols.person
                emptyText: pageModel.fans.hidden ? qsTr("TA 没有公开粉丝列表")
                         : page.their(qsTr("TA 还没有粉丝"), qsTr("还没有粉丝"))
            }
            People {
                visible: page.currentTab === 3
                people: pageModel.fans
            }
        }
    }

    // A titled grid of playlist cards; hidden when empty.
    component PlaylistSection: Column {
        id: section
        required property string title
        required property CollectionListModel items
        width: parent ? parent.width : 0
        visible: page.currentTab === 0 && items.count > 0
        spacing: 8
        Text {
            text: qsTr("%1 %2").arg(section.title).arg(section.items.count)
            color: Theme.color.onSurface
            font: Theme.type.titleMedium.font
            Accessible.role: Accessible.Heading
        }
        CollectionGrid {
            width: parent.width
            model: section.items
            onActivated: (kind, key) => page.shell.openCollection(kind, key)
        }
    }

    // Users as rows: avatar, nickname, signature, and a follow button for the signed-in
    // account (not on its own row); a row opens that user's page.
    component People: Column {
        id: list
        required property UserPeopleModel people
        width: Math.min(parent ? parent.width : 0, 720)
        spacing: 2
        Connections {
            target: list.people
            function onNotice(message: string) {
                page.shell.notify(message);
            }
        }
        Repeater {
            id: rows
            model: list.people.items
            delegate: ListItem {
                id: row
                required property int index
                required property string key
                required property string nickname
                required property string avatarKey
                required property string signature
                required property bool followed
                width: list.width
                segmented: true
                first: index === 0
                last: index === rows.count - 1
                containerColor: Theme.color.surfaceContainer
                text: nickname
                supportingText: signature
                leading: CoverArt {
                    width: 40
                    height: 40
                    radius: 20
                    glyph: Symbols.person
                    coverKey: row.avatarKey
                }
                trailing: App.session.signedIn && row.key !== App.session.accountKey
                          ? rowFollow : null
                onClicked: page.shell.openUser(row.key)

                Component {
                    id: rowFollow
                    Button {
                        size: Md.XSmall
                        variant: row.followed ? Md.Tonal : Md.Filled
                        text: row.followed ? qsTr("已关注") : qsTr("关注")
                        Accessible.name: (row.followed ? qsTr("取消关注 %1") : qsTr("关注 %1"))
                                         .arg(row.nickname)
                        onClicked: list.people.setFollowed(row.index, !row.followed)
                    }
                }
            }
        }
    }
}
