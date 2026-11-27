pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Album details (docs/ui/pages/album-daily.md §1) in the shared song page layout:
// type, title, aliases, artists, release and counts, the notes; save and comments.
// The songs keep the album's order (no sorting), numbered by track, without the
// cover and album columns.
SongCollectionPage {
    id: page
    origin: Playback.FromAlbum
    originId: pageModel.albumKey

    property alias albumKey: pageModel.albumKey
    shareKind: Share.Album
    shareKey: pageModel.albumKey
    shareAuthor: pageModel.artistText

    model: pageModel
    songs: pageModel.songs
    title: pageModel.title
    coverKey: pageModel.coverKey
    coverGlyph: Symbols.album
    sortable: false
    showCover: false
    albumColumn: false
    showAlbumAction: false
    numberByPosition: true
    actionSkeletons: [116, 88, 96]
    findPlaceholder: qsTr("在专辑中查找")
    noMatchText: qsTr("专辑里没有与「%1」相符的歌")
    emptyGlyph: Symbols.album
    emptyText: qsTr("这张专辑没有可显示的曲目")

    function toggleSubscribed(): void {
        if (!App.session.signedIn)
            page.shell.openLogin();
        else
            pageModel.subscribed = !pageModel.subscribed;
    }

    AlbumPageModel {
        id: pageModel
        onLibraryChanged: page.shell.refreshLibrary()
        onNotice: message => page.shell.notify(message)
    }

    // The notes: one line beside the cover when wide, under it otherwise.
    component Notes: Column {
        Text {
            id: notes
            width: parent.width
            text: pageModel.description
            maximumLineCount: page.twoPane ? 4 : 1
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
        Button {
            visible: notes.truncated
            variant: Md.Text
            size: Md.XSmall
            text: qsTr("更多")
            onClicked: notesDialog.open()
        }
    }

    infoContent: Column {
        spacing: 8

        KindLabel {
            text: pageModel.type
            color: page.infoAccentColor
            chip: page.twoPane
        }
        Column {
            width: parent.width
            Text {
                width: parent.width
                text: pageModel.title
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
        // Each artist opens their page.
        Flow {
            width: parent.width
            Repeater {
                model: pageModel.artists
                delegate: Row {
                    id: artist
                    required property int index
                    required property var modelData
                    Text {
                        text: artist.modelData.name
                        color: page.infoColor
                        font: Theme.type.titleMedium.font
                        // Underlined while hovered, like the table's links.
                        Rectangle {
                            y: parent.baselineOffset + 2
                            width: parent.width
                            height: 1
                            visible: artistHover.hovered
                            color: parent.color
                        }
                        HoverHandler {
                            id: artistHover
                            enabled: artist.modelData.key !== ""
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            enabled: artist.modelData.key !== ""
                            onTapped: page.shell.openArtist(artist.modelData.key)
                        }
                    }
                    Text {
                        visible: artist.index < pageModel.artists.length - 1
                        text: qsTr(" / ")
                        color: page.infoVariantColor
                        font: Theme.type.titleMedium.font
                    }
                }
            }
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: pageModel.releaseText
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
        Text {
            width: parent.width
            text: pageModel.countText
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
        Notes {
            width: parent.width
            visible: page.wide && pageModel.description !== ""
        }
    }

    extraContent: Notes {}
    showExtra: !page.wide && pageModel.description !== ""

    actionsContent: Flow {
        spacing: 8
        PageAction {
            visible: pageModel.subscribedKnown
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
            onClicked: page.shell.openComments(CommentsPageModel.Album, pageModel.albumKey,
                                               pageModel.title, pageModel.artistText,
                                               pageModel.coverKey)
        }
    }

    showNotice: pageModel.state === LoadState.Partial
    noticeContent: NoticeBar {
        text: qsTr("曲目列表可能不完整")
    }

    Dialog {
        id: notesDialog
        reading: true
        title: pageModel.title
        supportingText: pageModel.description
    }
}
