pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Podcast page (docs/ui/pages/search.md §3.4) in the shared song page layout: cover,
// title, description and the voice count; play all, save, newest or oldest first.
// The voices are a paged table (episode number, title with the podcast, duration),
// loaded alongside the header; no in-page find (the list comes in pages).
SongCollectionPage {
    id: page

    property alias podcastKey: pageModel.podcastKey
    shareKind: Share.Podcast
    shareKey: pageModel.podcastKey

    model: pageModel
    songs: pageModel.voices.songs
    title: pageModel.title
    coverKey: pageModel.coverKey
    coverGlyph: Symbols.podcasts
    voices: true
    sortable: false
    findable: false
    albumColumn: false
    showLiked: false
    showAlbumAction: false
    actionSkeletons: [116, 112, 132]
    emptyGlyph: Symbols.podcasts
    emptyText: qsTr("这个播客还没有节目")
    showSongs: pageModel.voices.state === LoadState.Ready
               || pageModel.voices.state === LoadState.Partial
    showBody: !showSongs
    footerContent: LoadMoreFooter {
        model: pageModel.voices
    }

    function toggleSubscribed(): void {
        if (!App.session.signedIn)
            page.shell.openLogin();
        else
            pageModel.subscribed = !pageModel.subscribed;
    }

    // The next page once the last rows are near.
    function loadMoreIfNear(): void {
        const voices = pageModel.voices;
        if (voices.canLoadMore && !voices.loadingMore && voices.loadMoreError.kind < 0
                && page.table.lastVisibleRow >= voices.songs.count - 20)
            voices.loadMore();
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

    PodcastPageModel {
        id: pageModel
        onLibraryChanged: page.shell.refreshLibrary()
        onNotice: message => page.shell.notify(message)
    }

    // The description: one line beside the cover when wide, under it otherwise.
    component Description: Column {
        Text {
            id: description
            width: parent.width
            text: pageModel.description
            maximumLineCount: page.twoPane ? 4 : 1
            wrapMode: Text.Wrap
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

    infoContent: Column {
        spacing: 8

        KindLabel {
            text: qsTr("播客")
            color: page.infoAccentColor
            chip: page.twoPane
        }
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
            visible: text !== ""
            text: pageModel.countText
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
        Description {
            width: parent.width
            visible: page.wide && pageModel.description !== ""
        }
    }

    extraContent: Description {}
    showExtra: !page.wide && pageModel.description !== ""

    actionsContent: Flow {
        spacing: 8
        PageAction {
            iconOnly: page.twoPane
            size: page.paneActionSize
            glyph: pageModel.subscribed ? Symbols.bookmark_added : Symbols.bookmark_add
            text: pageModel.subscribed ? qsTr("已收藏") : qsTr("收藏播客")
            onClicked: page.toggleSubscribed()
        }
        // Newest or oldest first; loads from the start.
        ConnectedButtonGroup {
            exclusive: true
            Button {
                variant: Md.Tonal
                checkable: true
                checked: !pageModel.voices.oldestFirst
                text: qsTr("最新")
                onClicked: pageModel.voices.oldestFirst = false
            }
            Button {
                variant: Md.Tonal
                checkable: true
                checked: pageModel.voices.oldestFirst
                text: qsTr("最早")
                onClicked: pageModel.voices.oldestFirst = true
            }
        }
    }

    // The voices' own loading or failure, under the header.
    bodyContent: TabState {
        model: pageModel.voices
        emptyGlyph: Symbols.podcasts
        emptyText: qsTr("这个播客还没有节目")
    }

    Dialog {
        id: descriptionDialog
        reading: true
        title: pageModel.title
        supportingText: pageModel.description
    }
}
