pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// A song's own page (docs/ui/pages/song.md): the header and actions, then its wiki,
// similar songs and similar playlists, each section loading, failing and hiding on its
// own. One scrolling column, no tabs; the album page's header family.
Item {
    id: page

    required property AppShell shell
    property alias songKey: pageModel.songKey

    readonly property bool wide: width >= 840
    readonly property real contentX: Math.max(24, (width - 1200) / 2)
    readonly property real contentWidth: Math.min(width - 48, 1200)
    readonly property var song: pageModel.song
    // Similar songs shown before "展开全部".
    readonly property int similarRows: 10
    property bool similarExpanded: false

    SongPageModel {
        id: pageModel
    }

    function play(): void {
        App.player.playSongs([page.song.key], 0, Playback.FromSong, page.song.key,
                             page.song.title);
    }
    function copyInfo(): void {
        const parts = [page.song.title, page.song.artistText, page.song.albumTitle];
        App.copyText(parts.filter(part => part !== "").join(" - "));
        page.shell.notify(qsTr("已复制歌曲信息"));
    }

    PageState {
        id: pageState
        anchors.fill: parent
        model: pageModel
        emptyGlyph: Symbols.music_note
        emptyText: qsTr("找不到这首歌")
        skeleton: Item {
            Skeleton {
                x: page.contentX
                y: 24
                width: page.wide ? 200 : 140
                height: width
                radius: Theme.shape.large
            }
            Column {
                x: page.contentX + (page.wide ? 224 : 164)
                y: 24 + (page.wide ? 40 : 24)
                spacing: 12
                Skeleton {
                    width: 280
                    height: page.wide ? 36 : 28
                }
                Skeleton {
                    width: 200
                    height: 20
                }
                Skeleton {
                    width: 240
                    height: 20
                }
            }
        }
    }

    // A link-styled piece of the header (an artist, the album).
    component Link: Text {
        id: link
        property bool clickable: true
        signal clicked
        color: clickable && linkHover.hovered ? Theme.color.primary : Theme.color.onSurfaceVariant
        font: Theme.type.bodyLarge.font
        elide: Text.ElideRight
        activeFocusOnTab: clickable
        Accessible.role: clickable ? Accessible.Link : Accessible.StaticText
        Accessible.name: text
        Keys.onReturnPressed: link.clicked()
        Keys.onEnterPressed: link.clicked()
        Keys.onSpacePressed: link.clicked()
        HoverHandler {
            id: linkHover
            enabled: link.clickable
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: link.clickable
            onTapped: link.clicked()
        }
        FocusRing {
            shown: link.activeFocus && InputMode.keyboard
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        opacity: pageState.contentOpacity
        visible: opacity > 0
        contentHeight: content.implicitHeight + 48
        bottomMargin: PageInsets.bottom
        boundsBehavior: Flickable.DragAndOvershootBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: content
            x: page.contentX
            y: 24
            width: page.contentWidth
            spacing: 24

            // The header: cover, type, title, artists, album · date · length.
            Item {
                width: parent.width
                height: Math.max(cover.height, info.height)

                CoverArt {
                    id: cover
                    width: page.wide ? 200 : 140
                    height: width
                    radius: Theme.shape.large
                    glyph: Symbols.music_note
                    coverKey: page.song.coverKey
                }
                Column {
                    id: info
                    x: cover.width + 24
                    width: parent.width - x
                    anchors.verticalCenter: page.wide ? undefined : cover.verticalCenter
                    spacing: 8

                    Text {
                        text: qsTr("歌曲")
                        color: Theme.color.primary
                        font: Theme.type.labelLarge.font
                    }
                    Column {
                        width: parent.width
                        Text {
                            width: parent.width
                            text: page.song.title
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            color: Theme.color.onSurface
                            font: page.wide ? Theme.type.headlineLargeEmphasized.font
                                            : Theme.type.headlineSmallEmphasized.font
                            Accessible.role: Accessible.Heading
                        }
                        Text {
                            width: parent.width
                            visible: text !== ""
                            text: page.song.secondaryTitle
                            elide: Text.ElideRight
                            color: Theme.color.onSurfaceVariant
                            font: Theme.type.bodyMedium.font
                        }
                    }
                    // Each artist on its own; " / " between them.
                    Flow {
                        width: parent.width
                        Repeater {
                            model: page.song.artists
                            delegate: Row {
                                id: artistItem
                                required property var modelData
                                required property int index
                                Text {
                                    visible: artistItem.index > 0
                                    text: " / "
                                    color: Theme.color.onSurfaceVariant
                                    font: Theme.type.bodyLarge.font
                                }
                                Link {
                                    text: artistItem.modelData.name
                                    clickable: artistItem.modelData.key !== ""
                                    onClicked: page.shell.openArtist(artistItem.modelData.key)
                                }
                            }
                        }
                    }
                    Flow {
                        width: parent.width
                        Link {
                            visible: text !== ""
                            text: page.song.albumTitle
                            clickable: page.song.albumKey !== ""
                            font: Theme.type.bodyMedium.font
                            onClicked: page.shell.openAlbum(page.song.albumKey)
                        }
                        Text {
                            readonly property var parts: [pageModel.dateText,
                                page.song.durationMs >= 0 ? App.format.duration(page.song.durationMs)
                                                          : ""].filter(part => part !== "")
                            visible: parts.length > 0
                            text: (page.song.albumTitle !== "" ? " · " : "") + parts.join(" · ")
                            color: Theme.color.onSurfaceVariant
                            font: Theme.type.bodyMedium.font
                        }
                    }
                }
            }

            // Play, like, comments, more.
            Row {
                spacing: 8
                Button {
                    variant: Md.Filled
                    size: Md.Medium
                    glyph: Symbols.play_arrow
                    text: qsTr("播放")
                    enabled: !page.song.unavailable && page.song.key !== ""
                    onClicked: page.play()
                }
                IconButton {
                    readonly property bool liked: App.library.likedRevision >= 0
                                                  && App.library.isLiked(page.song.key)
                    anchors.verticalCenter: parent.verticalCenter
                    variant: Md.Tonal
                    glyph: Symbols.favorite
                    iconFill: liked ? 1 : 0
                    pops: true
                    text: liked ? qsTr("取消喜欢") : qsTr("喜欢")
                    onClicked: {
                        if (!App.session.signedIn)
                            page.shell.openLogin();
                        else
                            App.library.setLiked(page.song.key, !liked);
                    }
                }
                Button {
                    anchors.verticalCenter: parent.verticalCenter
                    variant: Md.Tonal
                    glyph: Symbols.comment
                    text: qsTr("评论")
                    onClicked: page.shell.openSongComments(page.song.key, page.song.title,
                                                           page.song.artistText, page.song.coverKey)
                }
                IconButton {
                    id: moreButton
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Symbols.more_horiz
                    text: qsTr("更多")
                    onClicked: moreMenu.popup(moreButton, 0, moreButton.height + 4)
                }
            }

            HomeSection {
                width: parent.width
                title: qsTr("百科")
                model: pageModel.wiki
                skeleton: Column {
                    spacing: 12
                    Skeleton {
                        width: Math.min(420, page.contentWidth)
                        height: 32
                    }
                    Skeleton {
                        width: page.contentWidth
                        height: 160
                        radius: Theme.shape.large
                    }
                }

                WikiContent {
                    width: page.contentWidth
                }
            }

            HomeSection {
                width: parent.width
                title: qsTr("相似歌曲")
                model: pageModel.similarSongs
                skeleton: Column {
                    spacing: 2
                    Repeater {
                        model: 4
                        Skeleton {
                            width: page.contentWidth
                            height: 72
                            radius: Theme.shape.medium
                        }
                    }
                }
                trailing: Component {
                    Button {
                        variant: Md.Tonal
                        size: Md.Small
                        glyph: Symbols.play_arrow
                        text: qsTr("播放全部")
                        onClicked: App.player.playSongs(similarList.songs.keys(), 0,
                                                        Playback.FromSong, page.song.key,
                                                        page.song.title)
                    }
                }

                SongList {
                    id: similarList
                    width: page.contentWidth
                    height: heightFor(shownRows)
                    interactive: false
                    sideMargin: 0
                    bottomMargin: 0
                    model: pageModel.similarSongs.songs
                    rowLimit: page.similarExpanded ? 0 : page.similarRows
                    origin: Playback.FromSong
                    originId: page.song.key
                    originName: page.song.title
                    onAlbumRequested: key => page.shell.openAlbum(key)
                    onArtistRequested: key => page.shell.openArtist(key)
                    onSongRequested: key => page.shell.openSong(key)
                }
                Item {
                    width: page.contentWidth
                    height: 48
                    visible: !page.similarExpanded && similarList.count > page.similarRows
                    Button {
                        anchors.centerIn: parent
                        variant: Md.Text
                        trailingGlyph: Symbols.expand_more
                        text: qsTr("展开全部 %1 首").arg(similarList.count)
                        onClicked: page.similarExpanded = true
                    }
                }
            }

            HomeSection {
                width: parent.width
                title: qsTr("相似歌单")
                model: pageModel.similarPlaylists
                skeleton: Row {
                    spacing: 16
                    Repeater {
                        model: Math.max(1, Math.floor(page.contentWidth / 176))
                        Skeleton {
                            width: 160
                            height: 200
                            radius: Theme.shape.large
                        }
                    }
                }

                CollectionGrid {
                    width: page.contentWidth + 16
                    x: -8
                    minimumCardWidth: 176
                    maximumRows: 1
                    model: pageModel.similarPlaylists.items
                    onActivated: (kind, key) => page.shell.openCollection(kind, key)
                }
            }
        }
    }

    // The wiki: tag cards, then the text sections, then where it comes from.
    component WikiContent: Column {
        id: wiki
        spacing: 16

        // The label column, shared by every section so their texts line up: as wide as
        // the longest label, at most 120.
        readonly property real labelWidth: {
            let widest = 0;
            for (const section of pageModel.wiki.sections)
                for (const row of section.rows)
                    widest = Math.max(widest, labelMetrics.advanceWidth(row.label));
            return Math.min(120, Math.ceil(widest));
        }
        FontMetrics {
            id: labelMetrics
            font: Theme.type.bodyMedium.font
        }

        Flow {
            width: wiki.width
            visible: pageModel.wiki.cards.length > 0
            spacing: 24
            Repeater {
                model: pageModel.wiki.cards
                delegate: Row {
                    id: card
                    required property var modelData
                    spacing: 8
                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData.title + "：" + modelData.values.join("、")
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: card.modelData.title
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.labelLarge.font
                    }
                    Repeater {
                        model: card.modelData.values
                        delegate: Rectangle {
                            id: tag
                            required property string modelData
                            anchors.verticalCenter: parent.verticalCenter
                            width: tagText.implicitWidth + 24
                            height: 32
                            radius: Theme.shape.small
                            color: Theme.color.surfaceContainerHigh
                            Text {
                                id: tagText
                                anchors.centerIn: parent
                                text: tag.modelData
                                color: Theme.color.onSurface
                                font: Theme.type.labelLarge.font
                            }
                        }
                    }
                }
            }
        }

        Repeater {
            model: pageModel.wiki.sections
            delegate: Rectangle {
                id: section
                required property var modelData
                width: wiki.width
                height: sectionColumn.implicitHeight + 32
                radius: Theme.shape.large
                color: Theme.color.surfaceContainerLow

                Column {
                    id: sectionColumn
                    x: 20
                    y: 16
                    width: parent.width - 40
                    spacing: 10

                    Text {
                        visible: text !== ""
                        text: section.modelData.title
                        color: Theme.color.onSurface
                        font: Theme.type.titleSmall.font
                        Accessible.role: Accessible.Heading
                    }
                    SelectableText {
                        width: parent.width
                        visible: text !== ""
                        text: section.modelData.description
                        wrapMode: Text.Wrap
                        font: Theme.type.bodyMedium.font
                    }
                    Repeater {
                        model: section.modelData.rows
                        delegate: Item {
                            id: row
                            required property var modelData
                            width: sectionColumn.width
                            height: Math.max(rowLabel.implicitHeight, rowText.implicitHeight)
                            Accessible.role: Accessible.StaticText
                            Accessible.name: modelData.label + "：" + modelData.text
                            Text {
                                id: rowLabel
                                width: wiki.labelWidth
                                text: row.modelData.label
                                wrapMode: Text.Wrap
                                color: Theme.color.onSurfaceVariant
                                font: Theme.type.bodyMedium.font
                            }
                            SelectableText {
                                id: rowText
                                x: wiki.labelWidth + 16
                                width: parent.width - x
                                text: row.modelData.text
                                wrapMode: Text.Wrap
                                color: Theme.color.onSurface
                                font: Theme.type.bodyMedium.font
                            }
                        }
                    }
                }
            }
        }

        Text {
            text: qsTr("资料来自网易云音乐")
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodySmall.font
        }
    }

    Menu {
        id: moreMenu
        MenuItem {
            text: qsTr("下一首播放")
            glyph: Symbols.queue_play_next
            enabled: !page.song.unavailable
            onTriggered: App.player.addSongs([page.song.key], true, Playback.FromSong,
                                             page.song.key, page.song.title)
        }
        MenuItem {
            text: qsTr("加到播放队列")
            glyph: Symbols.add_to_queue
            enabled: !page.song.unavailable
            onTriggered: App.player.addSongs([page.song.key], false, Playback.FromSong,
                                             page.song.key, page.song.title)
        }
        MenuItem {
            text: qsTr("收藏到歌单…")
            glyph: Symbols.playlist_add
            onTriggered: page.shell.addToPlaylist([page.song.key])
        }
        MenuItem {
            text: qsTr("复制链接")
            glyph: Symbols.link
            onTriggered: App.share.copy(Share.Song, page.song.key, page.song.title,
                                        page.song.artistText)
        }
        MenuItem {
            text: qsTr("复制歌曲信息")
            glyph: Symbols.content_copy
            onTriggered: page.copyInfo()
        }
    }
}
