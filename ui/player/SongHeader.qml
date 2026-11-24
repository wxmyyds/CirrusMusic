pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Title and artist of the playing song with its "more" menu (docs/ui/pages/player.md
// §1.1). The artist opens the artist page; the menu has comments, the artist, the
// album and adding to a playlist. Each leaves the player first (the page's signals).
Item {
    id: root

    required property PlayerPalette colors
    // Smaller type in the narrow header row.
    property bool compact: false
    // The title's size; the page grows it with the cover.
    property real titleSize: 24
    // The "more" button right after the text instead of at the right edge (the bottom
    // row, where the header is wide).
    property bool moreAfterText: false

    signal commentsRequested
    // The song's own page (the title, or "歌曲详情").
    signal songRequested
    signal artistRequested(string key)
    signal albumRequested(string key)
    signal podcastRequested(string key) // a voice's podcast
    signal addToPlaylistRequested(string key, string title)
    signal copyRequested
    // Listen together's dialog (pages/together.md §1).
    signal togetherRequested

    readonly property var song: App.player.current
    readonly property bool menuOpen: menu.opened
    // The title and artist, for the flight from the mini player.
    readonly property Item textItem: texts

    implicitHeight: Math.max(texts.implicitHeight, more.implicitHeight)

    Column {
        id: texts
        readonly property real room: root.width - more.width - 8
                                     - (together.visible ? together.width + 4 : 0)
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: root.moreAfterText ? Math.min(room, Math.max(titleText.implicitWidth, artist.implicitWidth))
                                  : room
        spacing: 2

        Text {
            id: titleText
            width: Math.min(parent.width, implicitWidth)
            text: root.song.title
            elide: Text.ElideRight
            color: root.colors.content
            font: Fonts.font(700, root.compact ? 17 : root.titleSize)
            Accessible.role: Accessible.Heading

            HoverHandler {
                id: titleHover
                enabled: root.song.key !== "" && !root.song.voice
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                enabled: titleHover.enabled
                onTapped: root.songRequested()
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 2
                height: 1
                color: parent.color
                visible: titleHover.hovered
            }
        }
        Text {
            id: artist
            width: Math.min(parent.width, implicitWidth)
            text: App.player.status === Playback.Failed ? App.player.failureMessage
                                                        : root.song.artistText
            elide: Text.ElideRight
            color: root.colors.contentVariant
            font: Fonts.font(500, root.compact ? 14 : root.titleSize * 0.75)

            HoverHandler {
                id: artistHover
                enabled: root.song.artistKeys.length > 0
                cursorShape: Qt.PointingHandCursor
            }
            // One artist: there; several: a menu of them.
            TapHandler {
                enabled: root.song.artistKeys.length > 0
                onTapped: {
                    if (root.song.artistKeys.length === 1)
                        root.artistRequested(root.song.artistKeys[0]);
                    else
                        artistMenu.popup(artist, 0, artist.height + 4);
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: parent.color
                visible: artistHover.hovered
            }
        }
    }

    // Listen together, beside "more": filled while in a room, a warning while it reconnects.
    PlayerButton {
        id: together
        x: root.moreAfterText ? more.x + more.width + 4 : more.x - width - 4
        anchors.verticalCenter: parent.verticalCenter
        visible: root.song.key !== "" && App.together.available
        color: root.colors.content
        glyph: App.together.active && !App.together.connected ? Symbols.sync_problem
                                                              : Symbols.group
        iconFill: App.together.active ? 1 : 0
        text: !App.together.active ? qsTr("一起听")
              : App.together.partnerName !== "" ? qsTr("一起听 · 你和 %1").arg(App.together.partnerName)
              : qsTr("一起听 · 等好友加入")
        onClicked: root.togetherRequested()
    }

    PlayerButton {
        id: more
        x: root.moreAfterText ? texts.width + 8 : root.width - width
        anchors.verticalCenter: parent.verticalCenter
        visible: root.song.key !== ""
        color: root.colors.content
        glyph: Symbols.more_horiz
        text: qsTr("更多")
        onClicked: menu.popup(more, 0, more.height + 4)
    }

    Menu {
        id: menu
        MenuItem {
            text: qsTr("歌曲详情")
            glyph: Symbols.info
            enabled: !root.song.voice
            onTriggered: root.songRequested()
        }
        MenuItem {
            text: qsTr("评论")
            glyph: Symbols.comment
            enabled: !root.song.voice
            onTriggered: root.commentsRequested()
        }
        Instantiator {
            model: root.song.artists
            delegate: MenuItem {
                required property var modelData
                enabled: modelData.key !== ""
                glyph: Symbols.person
                text: root.song.artists.length > 1 ? qsTr("查看歌手：%1").arg(modelData.name)
                                                   : qsTr("查看歌手")
                onTriggered: root.artistRequested(modelData.key)
            }
            // After 评论.
            onObjectAdded: (index, object) => menu.insertItem(index + 2, object as MenuItem)
            onObjectRemoved: (index, object) => menu.removeItem(object as MenuItem)
        }
        MenuItem {
            text: root.song.voice ? qsTr("查看播客") : qsTr("查看专辑")
            glyph: root.song.voice ? Symbols.podcasts : Symbols.album
            enabled: root.song.albumKey !== ""
            onTriggered: root.song.voice ? root.podcastRequested(root.song.albumKey)
                                         : root.albumRequested(root.song.albumKey)
        }
        MenuItem {
            text: qsTr("加入歌单")
            glyph: Symbols.playlist_add
            enabled: !root.song.voice
            onTriggered: root.addToPlaylistRequested(root.song.key, root.song.title)
        }
        MenuItem {
            text: qsTr("复制链接")
            glyph: Symbols.link
            enabled: root.song.key !== ""
            onTriggered: App.share.copy(root.song.voice ? Share.Voice : Share.Song, root.song.key,
                                        root.song.title, root.song.artistText)
        }
        MenuItem {
            text: qsTr("复制歌曲信息")
            glyph: Symbols.content_copy
            onTriggered: root.copyRequested()
        }
        MenuItem {
            text: qsTr("一起听…")
            glyph: Symbols.group
            onTriggered: root.togetherRequested()
        }
    }

    // The artists, when the artist line is clicked and there are several.
    Menu {
        id: artistMenu
        Instantiator {
            model: root.song.artists
            delegate: MenuItem {
                required property var modelData
                enabled: modelData.key !== ""
                glyph: Symbols.person
                text: modelData.name
                onTriggered: root.artistRequested(modelData.key)
            }
            onObjectAdded: (index, object) => artistMenu.insertItem(index, object as MenuItem)
            onObjectRemoved: (index, object) => artistMenu.removeItem(object as MenuItem)
        }
    }
}
