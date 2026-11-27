pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Playlist details (docs/ui/pages/playlist.md) in the shared song page layout: the
// header has the creator, description, tags and counts; save (someone else's) or
// edit (the account's own: info, song order, delete); the songs sort by column.
SongCollectionPage {
    id: page
    origin: Playback.FromPlaylist
    originId: pageModel.playlistKey

    property alias playlistKey: pageModel.playlistKey
    shareKind: Share.Playlist
    shareKey: pageModel.playlistKey
    shareAuthor: pageModel.creator

    model: pageModel
    songs: pageModel.songs
    title: pageModel.title
    coverKey: pageModel.coverKey
    removable: pageModel.ownKnown && pageModel.own
    actionSkeletons: [116, 88, 96]
    onRemoveRequested: keys => page.removeSongs(keys)
    emptyText: pageModel.own ? qsTr("这个歌单还没有歌。在歌曲的菜单里选择「收藏到歌单」就能加进来。")
                             : qsTr("这个歌单还没有歌")

    // Asks first (menu, Delete).
    function removeSongs(keys: list<string>): void {
        removeDialog.songKeys = keys;
        removeDialog.open();
    }

    function toggleSubscribed(): void {
        if (!App.session.signedIn)
            page.shell.openLogin();
        else
            pageModel.subscribed = !pageModel.subscribed;
    }

    PlaylistPageModel {
        id: pageModel
        onLibraryChanged: page.shell.refreshLibrary()
        onNotice: message => page.shell.notify(message)
        onDeleted: page.shell.back()
    }

    // Large playlists: details for the rows on screen and around them; sorting and
    // finding need every song's.
    function loadDetails(): void {
        if (page.sorted.sortField !== SortedSongsModel.Original || page.findText.trim() !== "")
            pageModel.loadAll();
        else
            pageModel.ensureLoaded(page.table.firstVisibleRow - 20, page.table.lastVisibleRow + 20);
    }
    Connections {
        target: page.table
        function onFirstVisibleRowChanged() {
            page.loadDetails();
        }
        function onLastVisibleRowChanged() {
            page.loadDetails();
        }
    }
    Connections {
        target: page.sorted
        function onSortChanged() {
            page.loadDetails();
        }
    }
    Connections {
        target: pageModel.songs
        function onCountChanged() {
            Qt.callLater(page.loadDetails);
        }
    }
    onFindTextChanged: loadDetails()

    // Description and tags: beside the cover when wide, under it otherwise.
    component Extras: Column {
        spacing: 12

        Column {
            width: parent.width
            visible: pageModel.description !== ""
            Text {
                id: description
                width: parent.width
                text: pageModel.description
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: page.infoVariantColor
                font: Theme.type.bodyMedium.font
            }
            Button {
                visible: description.truncated
                variant: Md.Text
                size: Md.XSmall
                text: qsTr("更多")
                onClicked: descriptionDialog.open()
            }
        }
        Flow {
            width: parent.width
            visible: pageModel.tags.length > 0
            spacing: 8
            Repeater {
                model: pageModel.tags
                Chip {
                    required property string modelData
                    text: modelData
                    // Until the discover page has categories, a tag searches.
                    onClicked: page.shell.search(modelData)
                }
            }
        }
    }

    infoContent: Column {
        spacing: 8

        KindLabel {
            text: qsTr("歌单")
            color: page.infoAccentColor
            chip: page.twoPane
        }
        // The title; the account's own playlist can be edited from here.
        Item {
            width: parent.width
            height: Math.max(titleText.height, editButton.visible ? editButton.height : 0)
            Text {
                id: titleText
                width: Math.min(implicitWidth, parent.width
                                - (editButton.visible ? editButton.width + 8 : 0))
                anchors.verticalCenter: parent.verticalCenter
                text: pageModel.title
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: page.infoColor
                font: page.titleFont
                Accessible.role: Accessible.Heading
            }
            IconButton {
                id: editButton
                x: titleText.width + 8
                anchors.verticalCenter: parent.verticalCenter
                // "我喜欢的音乐" keeps its title.
                visible: pageModel.ownKnown && pageModel.own
                         && pageModel.likedListKnown && !pageModel.likedList
                variant: page.twoPane ? Md.Tonal : Md.Standard
                glyph: Symbols.edit
                text: qsTr("编辑歌单信息")
                onClicked: editDialog.open()
            }
        }
        // The creator (opens their user page), then when it was updated; the button's
        // label lines up with the text above.
        Row {
            x: creatorButton.visible ? -creatorButton.leftPadding : 0
            width: parent.width - x
            spacing: 4
            Button {
                id: creatorButton
                visible: pageModel.creator !== "" && pageModel.creatorKey !== ""
                anchors.verticalCenter: parent.verticalCenter
                variant: Md.Text
                size: Md.XSmall
                text: pageModel.creator
                onClicked: page.shell.openUser(pageModel.creatorKey)
            }
            Text {
                width: parent.width - (creatorButton.visible ? creatorButton.width + 4 : 0)
                anchors.verticalCenter: parent.verticalCenter
                text: [creatorButton.visible ? "" : pageModel.creator, pageModel.updatedText]
                      .filter(s => s !== "").join(" · ")
                elide: Text.ElideRight
                color: page.infoVariantColor
                font: Theme.type.bodyMedium.font
            }
        }
        // Counts the service gave; unknown ones are left out, not shown as 0.
        Text {
            width: parent.width
            text: {
                const parts = [qsTr("%1 首").arg(App.format.count(pageModel.songs.count))];
                if (pageModel.playCount >= 0)
                    parts.push(qsTr("播放 %1 次").arg(App.format.count(pageModel.playCount)));
                if (pageModel.subscriberCount >= 0)
                    parts.push(qsTr("收藏 %1").arg(App.format.count(pageModel.subscriberCount)));
                return parts.join(" · ");
            }
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
        Extras {
            width: parent.width
            visible: page.wide && (pageModel.description !== "" || pageModel.tags.length > 0)
            topPadding: 4
            bottomPadding: 4
        }
    }

    extraContent: Extras {}
    showExtra: !page.wide && (pageModel.description !== "" || pageModel.tags.length > 0)

    actionsContent: Flow {
        spacing: 8
        // The account's liked songs: 心动模式 from their start (heartbeat-mode.md §7.2).
        PageAction {
            visible: pageModel.likedListKnown && pageModel.likedList && page.playlistKey !== ""
                     && page.playlistKey === App.library.likedPlaylistKey
            enabled: !App.player.heartbeatLoading
            iconOnly: page.twoPane
            size: page.paneActionSize
            glyph: Symbols.ecg_heart
            text: qsTr("心动模式")
            onClicked: App.player.startHeartbeatWith(page.shownKeys(), pageModel.title)
        }
        // Someone else's playlist (P5).
        PageAction {
            visible: pageModel.ownKnown && !pageModel.own
                     && pageModel.subscribedKnown && App.library.canSavePlaylists
            iconOnly: page.twoPane
            size: page.paneActionSize
            glyph: pageModel.subscribed ? Symbols.bookmark_added : Symbols.bookmark_add
            text: pageModel.subscribed ? qsTr("已收藏") : qsTr("收藏")
            onClicked: page.toggleSubscribed()
        }
        PageAction {
            iconOnly: page.twoPane
            size: page.paneActionSize
            variant: Md.Text
            glyph: Symbols.comment
            text: pageModel.commentCount > 0
                  ? qsTr("评论 %1").arg(App.format.count(pageModel.commentCount))
                  : qsTr("评论")
            onClicked: page.shell.openComments(CommentsPageModel.Playlist, page.playlistKey,
                                               pageModel.title, pageModel.creator,
                                               pageModel.coverKey)
        }
        IconButton {
            id: moreButton
            visible: pageModel.ownKnown && pageModel.own
            // In the pane: square and as tall as the play button, like the PageActions.
            variant: page.twoPane ? Md.Tonal : Md.Standard
            size: page.twoPane ? page.paneActionSize : Md.Small
            shape: page.twoPane ? Md.Square : Md.Round
            widthMode: page.twoPane ? Md.Narrow : Md.Default
            glyph: Symbols.more_vert
            text: qsTr("更多")
            onClicked: moreMenu.popup(moreButton, 0, moreButton.height + 4)

            Menu {
                id: moreMenu
                MenuItem {
                    visible: pageModel.ownKnown && pageModel.own
                             && pageModel.likedListKnown && !pageModel.likedList
                    height: visible ? implicitHeight : 0
                    glyph: Symbols.edit
                    text: qsTr("编辑歌单信息")
                    onTriggered: editDialog.open()
                }
                MenuItem {
                    visible: pageModel.ownKnown && pageModel.own
                    height: visible ? implicitHeight : 0
                    enabled: page.hasSongs && pageModel.ownKnown && pageModel.own
                    glyph: Symbols.swap_vert
                    text: qsTr("调整歌曲顺序")
                    onTriggered: page.shell.openPlaylistOrder(page.playlistKey)
                }
                MenuItem {
                    visible: pageModel.ownKnown && pageModel.own
                             && pageModel.likedListKnown && !pageModel.likedList
                    height: visible ? implicitHeight : 0
                    glyph: Symbols.delete_
                    text: qsTr("删除歌单")
                    onTriggered: deleteDialog.open()
                }
            }
        }
    }

    showNotice: pageModel.state === LoadState.Partial || pageModel.missingCount > 0
    noticeContent: NoticeBar {
        text: pageModel.missingCount > 0
              ? qsTr("有 %1 首歌暂时无法获取信息").arg(pageModel.missingCount)
              : qsTr("有些歌曲的信息暂时无法获取")
    }

    Dialog {
        id: descriptionDialog
        reading: true
        title: pageModel.title
        supportingText: pageModel.description
    }

    Dialog {
        id: editDialog
        width: 480
        title: qsTr("编辑歌单信息")
        acceptText: qsTr("保存")
        rejectText: qsTr("取消")
        acceptEnabled: pageModel.ownKnown && pageModel.own
                       && pageModel.likedListKnown && !pageModel.likedList
                       && titleField.text.trim() !== ""
        initialFocusItem: titleField
        onAboutToShow: {
            titleField.text = pageModel.title;
            descriptionField.text = pageModel.description;
        }
        onAccepted: pageModel.updateInfo(titleField.text, descriptionField.text)

        Column {
            width: parent.width
            spacing: 16
            TextField {
                id: titleField
                width: parent.width
                variant: Md.Outlined
                gapColor: Theme.color.surfaceContainerHigh
                label: qsTr("标题")
                error: text.trim() === ""
                supportingText: error ? qsTr("标题不能为空") : ""
            }
            TextField {
                id: descriptionField
                width: parent.width
                variant: Md.Outlined
                gapColor: Theme.color.surfaceContainerHigh
                label: qsTr("简介")
            }
        }
    }

    Dialog {
        id: removeDialog
        property list<string> songKeys
        title: songKeys.length > 1 ? qsTr("从歌单移除 %1 首歌？").arg(songKeys.length)
                                   : qsTr("从歌单移除这首歌？")
        supportingText: qsTr("移除后可以在歌曲的菜单里重新收藏到「%1」。").arg(pageModel.title)
        acceptText: qsTr("移除")
        acceptEnabled: pageModel.ownKnown && pageModel.own
        rejectText: qsTr("取消")
        onAccepted: pageModel.removeSongs(songKeys)
    }

    Dialog {
        id: deleteDialog
        title: qsTr("删除歌单？")
        supportingText: qsTr("「%1」会被删除，无法恢复。歌曲本身不受影响。").arg(pageModel.title)
        acceptText: qsTr("删除")
        acceptEnabled: pageModel.ownKnown && pageModel.own
                       && pageModel.likedListKnown && !pageModel.likedList
        rejectText: qsTr("取消")
        onAccepted: pageModel.deletePlaylist()
    }
}
