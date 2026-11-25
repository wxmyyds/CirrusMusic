pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The shell (docs/ui/pages/shell.md): navigation rail and a rounded content panel
// with the page stack. The shell starts at the window's top: the title bar row (with
// the TopBar) lies over its first `topInset`, the rail below it, and the panel
// reaches up under the row, so back, search and the window buttons sit on the panel
// (redesign-md3e.md §8); the mini player floats over the bottom of the panel (pages
// scroll under it, PageInsets). Large windows (≥ 1200)
// keep the rail expanded beside the panel and push it when toggled; narrower ones
// keep it collapsed and expand it over the panel with a scrim. Crossing the
// breakpoint switches without animation (motion-choreography.md §5). Pages get
// `shell` and navigate through it; back and forward keep a browser-like history.
Item {
    id: shell

    readonly property bool large: width >= 1200
    // The title bar row's height over the shell (pages start under it), and where the
    // panel starts.
    property real topInset: 0
    readonly property real panelTop: 8
    // The window's corner radius (rounded frames): what reaches its corners rounds so.
    property real windowCornerRadius: 0
    readonly property bool canGoBack: stack.depth > 1
    readonly property bool canGoForward: forwardEntries.length > 0
    // Width the rail takes beside the panel (the floating rail does not push).
    readonly property real navWidth: large ? rail.width : rail.collapsedWidth
    readonly property real railProgress: rail.progress
    readonly property bool railExpanded: rail.expanded
    readonly property bool searching: (stack.currentItem as SearchPage) !== null
    readonly property SearchModel searchModel: searchModelObject
    // The search input, in the top bar; Main connects it.
    property SearchField searchField
    // History like a browser's: every page visited is an entry of the page stack, and
    // `sections` holds the rail destination of each entry (for the rail's selection).
    // Back returns to the page visited before; forward revisits pages left with back
    // ({component, properties, section}) until something new is opened.
    property var sections: []
    property var forwardEntries: []
    // The library page's collection tab (library.md §2) and the discover page's
    // (discover.md §2), kept in App.settings.
    property int libraryTab: App.settings.libraryTab
    property int discoverTab: App.settings.discoverTab
    onLibraryTabChanged: App.settings.libraryTab = libraryTab
    onDiscoverTabChanged: App.settings.discoverTab = discoverTab
    // The account's playlists (the rail's, and the add-to-playlist dialog's).
    readonly property LibraryPageModel library: myLibrary
    // The player page covers the shell: its own keys (search, back, the rail) pause.
    property bool covered: false
    // For the player page's shared elements (MiniPlayer.bar, coverItem, titleItem,
    // controlsItem, seekItem, flying).
    // The player docked as a pane on the right (SidePlayer, App.settings.miniDocked)
    // where the content keeps at least 700 beside it; else the floating mini player.
    readonly property real sideWidth: Math.round(Math.max(340, Math.min(396, width * 0.27)))
    readonly property bool canDock: width - navWidth - sideWidth - 24 >= 700
    readonly property bool docked: App.settings.miniDocked && canDock
    readonly property bool sideShown: docked && App.player.status !== Playback.Empty
    // The content panel's right edge and middle, in the shell (the top bar centres the
    // search on it).
    readonly property real panelRight: panel.x + panel.width
    readonly property real panelCenter: panel.x + panel.width / 2
    // Where the player page grows from and its shared elements fly from: the side
    // pane's while docked, else the mini player's.
    readonly property Item miniBar: docked ? sidePlayer.bar : miniPlayer.bar
    readonly property Item miniCover: docked ? sidePlayer.coverItem : miniPlayer.coverItem
    readonly property Item miniTitle: docked ? sidePlayer.titleItem : miniPlayer.titleItem
    readonly property Item miniControls: docked ? sidePlayer.controlsItem : miniPlayer.controlsItem
    readonly property Item miniSeek: docked ? sidePlayer.seekItem : miniPlayer.seekItem
    // The side pane's cover is cut to a shape and turns: the page's flying cover
    // starts from it (-1: none, a plain rounded cover).
    readonly property int miniCoverShape: docked ? sidePlayer.coverShape : -1
    readonly property real miniCoverRotation: docked ? sidePlayer.coverRotation : 0
    readonly property real miniCoverCorner: docked ? sidePlayer.coverRadius : -1
    property bool miniFlying: false

    // The mini player asks for the player page, or for it with the queue open.
    signal playerRequested
    signal queueRequested
    // Something on top (the player page) should give way, e.g. for the settings.
    signal uncoverRequested
    // A notice while covered: whatever covers the shell shows it.
    signal coveredNotice(string text)
    // The playlist the current page shows, for the rail's playlist rows.
    readonly property string openPlaylistKey: (stack.currentItem as PlaylistPage)?.playlistKey ?? ""
    // The page's own item for 复制链接 (pages/share.md §3): {kind, key, title, author}, or
    // null on pages without one.
    readonly property var pageShare: {
        const collection = stack.currentItem as SongCollectionPage;
        if (collection && collection.shareKind >= 0 && collection.shareKey !== "")
            return {
                kind: collection.shareKind,
                key: collection.shareKey,
                title: collection.title,
                author: collection.shareAuthor
            };
        const songPage = stack.currentItem as SongPage;
        if (songPage && songPage.song.key !== "")
            return {
                kind: Share.Song,
                key: songPage.song.key,
                title: songPage.song.title,
                author: songPage.song.artistText
            };
        return null;
    }
    readonly property var destinations: [{
            glyph: Symbols.home,
            text: qsTr("首页"),
            page: homePage
        }, {
            glyph: Symbols.explore,
            text: qsTr("发现"),
            page: discoverPage
        }, {
            glyph: Symbols.library_music,
            text: qsTr("音乐库"),
            page: libraryPage
        }]

    function toggleRail() {
        rail.expanded = !rail.expanded;
        // Large windows remember it; a narrow window's rail only opens over the panel.
        if (large)
            App.settings.railExpanded = rail.expanded;
    }

    // A new page on top, in the current section; forward history ends here.
    function go(component: Component, properties: var) {
        visit(component, properties, rail.currentIndex, PageStack.Kind.Forward);
    }

    // A rail destination, as a new history entry (fading in). Nothing happens when it
    // is the page already shown.
    function select(index: int) {
        if (!large)
            rail.expanded = false;
        const top = stack.entries[stack.entries.length - 1];
        if (top && top.component === destinations[index].page)
            return;
        visit(destinations[index].page, {
            shell: shell
        }, index, PageStack.Kind.Fade);
    }

    function visit(component: Component, properties: var, section: int, transition: int) {
        forwardEntries = [];
        sections = sections.concat([section]);
        rail.currentIndex = section;
        const handoff = CoverHandoff.take();
        const page = stack.push(component, properties, transition);
        if (transition === PageStack.Kind.Forward)
            coverFlight.forward(handoff, page);
    }

    function openPlaylist(key: string) {
        shell.go(playlistPage, {
            shell: shell,
            playlistKey: key
        });
    }

    // The song order of the account's own playlist (playlist.md §13).
    function openPlaylistOrder(key: string) {
        shell.go(playlistOrderPage, {
            shell: shell,
            playlistKey: key
        });
    }

    function openAlbum(key: string) {
        shell.go(albumPage, {
            shell: shell,
            albumKey: key
        });
    }

    function openArtist(key: string) {
        shell.go(artistPage, {
            shell: shell,
            artistKey: key
        });
    }

    // A user's page (docs/ui/pages/user.md).
    function openUser(key: string) {
        shell.go(userPage, {
            shell: shell,
            userKey: key
        });
    }

    // The comments of a song, playlist or album (CommentsPageModel.Target); the
    // header shows `title`, `subtitle` and the cover the opening page knows.
    function openComments(target: int, key: string, title: string, subtitle: string,
                          coverKey: string) {
        shell.go(commentsPage, {
            shell: shell,
            target: target,
            targetKey: key,
            title: title,
            subtitle: subtitle,
            coverKey: coverKey
        });
    }

    // The song's comments, from a song list or the mini player.
    // A song's own page: wiki and similar songs, playlists (pages/song.md).
    function openSong(key: string) {
        if (key === "")
            return;
        shell.go(songPage, {
            shell: shell,
            songKey: key
        });
    }

    function openSongComments(key: string, title: string, artistText: string,
                              coverKey: string) {
        openComments(CommentsPageModel.Song, key, title, artistText, coverKey);
    }

    function openPodcast(key: string) {
        shell.go(podcastPage, {
            shell: shell,
            podcastKey: key
        });
    }

    function openSearch() {
        if (!searching)
            shell.go(searchPage, {
                shell: shell,
                search: shell.searchModel
            });
    }

    // Searches from anywhere: the search page shows the results.
    function search(text: string) {
        openSearch();
        searchModelObject.submit(text);
    }

    // A link the web opened the client with (App.linkRequested, docs/ui/pages/open-links.md):
    // its page, over the player page if that is open.
    function openLink(target: int, key: string) {
        if (target === App.LinkUnsupported) {
            notify(qsTr("暂不支持这类链接"));
            return;
        }
        if (covered)
            uncoverRequested();
        switch (target) {
        case App.LinkSong:
            openSong(key);
            break;
        case App.LinkPlaylist:
            openPlaylist(key);
            break;
        case App.LinkAlbum:
            openAlbum(key);
            break;
        case App.LinkArtist:
            openArtist(key);
            break;
        case App.LinkPodcast:
            openPodcast(key);
            break;
        case App.LinkUser:
            openUser(key);
            break;
        }
    }
    Connections {
        target: App
        function onLinkRequested(target: int, key: string) {
            shell.openLink(target, key);
        }
        // A voice link that couldn't play.
        function onNotice(message: string) {
            shell.notify(message);
        }
    }

    // A short message at the bottom of the content panel (Snackbar).
    function notify(text: string) {
        if (covered)
            coveredNotice(text);
        else
            snackbar.show(text);
    }
    // The same with one action (Snackbar.showAction); Main shows it on the player page
    // while that covers the shell.
    function notifyAction(text: string, action: string, handler: var) {
        snackbar.showAction(text, action, handler);
    }
    function focusMiniPlayer() {
        if (docked)
            sidePlayer.takeFocus();
        else
            miniPlayer.takeFocus();
    }
    // The player docked on the right, or floating again (remembered).
    function setDocked(on: bool): void {
        App.settings.miniDocked = on;
    }

    // "收藏到歌单" for `keys`: picks one of the account's playlists (sign-in first).
    function addToPlaylist(keys: list<string>) {
        if (!App.session.signedIn) {
            loginDialog.open();
            return;
        }
        addToPlaylistDialog.songKeys = keys;
        addToPlaylistDialog.open();
    }

    // The account's playlists changed (saved, created, deleted): the rail reloads.
    function refreshLibrary() {
        myLibrary.refresh();
    }

    // Keyboard focus back to the page, e.g. after Esc in the search field.
    function focusContent() {
        stack.currentItem?.forceActiveFocus();
    }

    // The field shows what the search page shows, and nothing elsewhere.
    onSearchingChanged: {
        if (!searchField)
            return;
        if (!searching)
            searchField.clear();
        else if (searchField.text === "")
            searchField.text = searchModel.query;
    }

    SearchModel {
        id: searchModelObject
        text: shell.searchField ? shell.searchField.text : ""
    }

    function openSettings() {
        if ((stack.currentItem as SettingsPage) === null)
            shell.go(settingsPage, {
                shell: shell
            });
    }

    // The account's "我喜欢的音乐" playlist (the library page until it is known).
    function openLikedPlaylist() {
        if (App.library.likedPlaylistKey !== "")
            openPlaylist(App.library.likedPlaylistKey);
        else
            select(2);
    }

    // A discover tab (0 playlists, 1 charts, 2 artists, 3 new albums): the home page's
    // "查看全部".
    function openDiscover(tab: int) {
        discoverTab = tab;
        select(1);
    }

    function openRecent() {
        shell.go(recentPage, {
            shell: shell
        });
    }

    function openRanking() {
        shell.go(rankingPage, {
            shell: shell
        });
    }

    // The local listening statistics (pages/stats.md), and its full song ranking for a
    // range and period (StatsPageModel.range, offset).
    function openStats() {
        shell.go(statsPage, {
            shell: shell
        });
    }

    function openStatsSongs(range: int, offset: int) {
        shell.go(statsSongsPage, {
            shell: shell,
            range: range,
            offset: offset
        });
    }

    // Today's recommended songs, or a past day's ("yyyy-MM-dd", album-daily.md §2.1).
    function openDaily(date: string) {
        shell.go(dailySongsPage, {
            shell: shell,
            date: date ?? ""
        });
    }

    // Listen together (pages/together.md): sign-in first.
    function openTogether() {
        if (App.session.signedIn)
            togetherDialog.open();
        else
            loginDialog.open();
    }
    // Personal FM leaves a listen-together room (LT5): asked first.
    function startFm() {
        if (App.together.state === TogetherState.Idle)
            App.player.startFm();
        else
            fmTogetherDialog.open();
    }

    // QR sign-in in a dialog, over the current page.
    function openLogin() {
        if (!App.session.signedIn)
            loginDialog.open();
    }

    // Collection.Kind and key, as collection cards report them.
    function openCollection(kind: int, key: string) {
        if (kind === Collection.Album)
            openAlbum(key);
        else if (kind === Collection.Artist)
            openArtist(key);
        else if (kind === Collection.Podcast)
            openPodcast(key);
        else
            openPlaylist(key);
    }

    function back() {
        if (stack.depth < 2)
            return;
        // Unsaved changes to a song order: the page asks first, then comes back here.
        const order = stack.currentItem as PlaylistOrderPage;
        if (order && order.hasChanges && !order.leaving) {
            order.leave();
            return;
        }
        const leaving = stack.entries[stack.entries.length - 1];
        if (!stack.pop())
            return;
        coverFlight.backward(leaving.item, stack.currentItem);
        // An editing page left is done with, not something to go forward to.
        if (leaving.component !== playlistOrderPage)
            forwardEntries = forwardEntries.concat([{
                    component: leaving.component,
                    properties: leaving.properties,
                    section: sections[sections.length - 1]
                }]);
        sections = sections.slice(0, -1);
        rail.currentIndex = sections[sections.length - 1];
    }

    function forward() {
        if (!canGoForward)
            return;
        const next = forwardEntries[forwardEntries.length - 1];
        forwardEntries = forwardEntries.slice(0, -1);
        sections = sections.concat([next.section]);
        rail.currentIndex = next.section;
        stack.push(next.component, next.properties,
                   next.component === destinations[next.section].page ? PageStack.Kind.Fade
                                                                      : PageStack.Kind.Forward);
    }

    Component.onCompleted: {
        rail.snap(large && App.settings.railExpanded);
        sections = [0];
        stack.replaceAll(homePage, {
            shell: shell
        });
    }
    onLargeChanged: rail.snap(large && App.settings.railExpanded)

    Component {
        id: homePage
        HomePage {}
    }
    Component {
        id: discoverPage
        DiscoverPage {}
    }
    Component {
        id: searchPage
        SearchPage {}
    }
    Component {
        id: libraryPage
        LibraryPage {}
    }
    Component {
        id: settingsPage
        SettingsPage {}
    }
    Component {
        id: playlistPage
        PlaylistPage {}
    }
    Component {
        id: playlistOrderPage
        PlaylistOrderPage {}
    }
    Component {
        id: albumPage
        AlbumPage {}
    }
    Component {
        id: artistPage
        ArtistPage {}
    }
    Component {
        id: userPage
        UserPage {}
    }
    Component {
        id: podcastPage
        PodcastPage {}
    }
    Component {
        id: commentsPage
        CommentsPage {}
    }
    Component {
        id: recentPage
        RecentPage {}
    }
    Component {
        id: rankingPage
        RankingPage {}
    }
    Component {
        id: songPage
        SongPage {}
    }
    Component {
        id: statsPage
        StatsPage {}
    }
    Component {
        id: statsSongsPage
        StatsSongsPage {}
    }
    Component {
        id: dailySongsPage
        DailySongsPage {}
    }

    // The content panel: rounded, on the frame color (Main sets the window's surface
    // to SurfaceContainer); the corners are masked so scrolled pages stay inside.
    Rectangle {
        id: panel
        x: shell.navWidth
        y: shell.panelTop
        width: shell.width - x - 8 - dock.value * shell.sideWidth
        height: shell.height - y - 8
        radius: Theme.shape.largeIncreased
        color: Theme.color.surface

        PageStack {
            id: stack
            anchors.fill: parent
            anchors.topMargin: Math.max(0, shell.topInset - panel.y)
            clip: true
            // The trail in the log: which page, with which ids (logging-v2 §4.1).
            onCurrentItemChanged: if (currentItem)
                                      App.diagnostics.pageShown(currentItem, depth,
                                                                entries[entries.length - 1].properties)
        }
        // A card's cover flying to the page it opened, and back (expressive.md §5).
        CoverFlight {
            id: coverFlight
            anchors.fill: stack
            current: stack.currentItem
        }

        Repeater {
            model: 4
            PanelCorner {
                required property int index
                radius: panel.radius
                color: Theme.color.surfaceContainer
                x: index === 1 || index === 2 ? panel.width - radius : 0
                y: index >= 2 ? panel.height - radius : 0
                rotation: index * 90
            }
        }

        Snackbar {
            id: snackbar
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16 + miniPlayer.reservedHeight
        }
    }

    // Slides the side pane in and narrows the panel (the rail's push, §5).
    Spring {
        id: dock
        target: shell.sideShown ? 1 : 0
        spec: Theme.motion.defaultSpatial
        Component.onCompleted: snapTo(target)
    }
    // The mini and side players wear the cover's colors (player-expressive.md §1).
    CoverColors {
        id: coverColors
        dark: Theme.dark
    }

    SidePlayer {
        id: sidePlayer
        ColorScope.scheme: coverColors.scheme
        // Flush with the window's top, right and bottom edges.
        x: shell.width - dock.value * shell.sideWidth
        width: shell.sideWidth
        height: shell.height
        visible: dock.value > 0.001
        edgeRadius: shell.windowCornerRadius
        topInset: shell.topInset
        flying: shell.miniFlying && shell.docked
        onOpenRequested: shell.playerRequested()
        onQueueRequested: shell.queueRequested()
        onLoginRequested: shell.openLogin()
    }

    MiniPlayer {
        id: miniPlayer
        ColorScope.scheme: coverColors.scheme
        hidden: shell.docked
        canDock: shell.canDock
        flying: shell.miniFlying && !shell.docked
        onOpenRequested: shell.playerRequested()
        onDockRequested: shell.setDocked(true)
        onQueueRequested: shell.queueRequested()
        onLoginRequested: shell.openLogin()
        onCommentsRequested: {
            const song = App.player.current;
            shell.openSongComments(song.key, song.title, song.artistText, song.coverKey);
        }
        x: panel.x
        width: panel.width
        y: panel.y + panel.height - implicitHeight - bottomMargin
    }
    // Pages keep their last rows clear of the bar.
    Binding {
        target: PageInsets
        property: "bottom"
        value: miniPlayer.reservedHeight
    }

    // Blocks the content while the rail floats over it.
    MouseArea {
        anchors.fill: parent
        anchors.topMargin: shell.topInset
        visible: !shell.large && rail.progress > 0
        hoverEnabled: true
        onClicked: rail.expanded = false
        onWheel: wheel => wheel.accepted = true

        Rectangle {
            anchors.fill: parent
            color: Theme.color.scrim
            opacity: 0.32 * rail.progress
        }
    }

    // The account's playlists for the rail.
    LibraryPageModel {
        id: myLibrary
    }

    NavigationRail {
        id: rail
        anchors.top: parent.top
        anchors.topMargin: shell.topInset
        anchors.bottom: parent.bottom
        modal: !shell.large
        minimumExpandedWidth: 272
        topInset: 8
        restColor: Theme.color.surfaceContainer
        expandedContent: Component {
            RailPlaylists {
                library: myLibrary
                selectedKey: shell.openPlaylistKey
                onPlaylistRequested: key => shell.openPlaylist(key)
                onLikedRequested: shell.openLikedPlaylist()
                onRecentRequested: shell.openRecent()
                onStatsRequested: shell.openStats()
                onSignInRequested: shell.openLogin()
            }
        }
        collapsedContent: Component {
            RailCovers {
                library: myLibrary
                selectedKey: shell.openPlaylistKey
                onPlaylistRequested: key => shell.openPlaylist(key)
            }
        }
        footer: Component {
            RailAccount {
                progress: rail.progress
                onSignInRequested: shell.openLogin()
                onSettingsRequested: shell.openSettings()
                onProfileRequested: shell.openUser(App.session.accountKey)
            }
        }
        onActivated: index => shell.select(index)

        Repeater {
            model: shell.destinations
            delegate: NavigationRailItem {
                required property var modelData
                glyph: modelData.glyph
                text: modelData.text
            }
        }
    }

    Shortcut {
        sequence: "Esc"
        enabled: !shell.covered && !shell.large && rail.expanded
        onActivated: rail.expanded = false
    }
    Shortcut {
        sequence: "Ctrl+K"
        enabled: !shell.covered
        onActivated: {
            shell.searchField.forceActiveFocus();
            shell.searchField.selectAll();
        }
    }
    // Pages with an in-page find take Ctrl+F; elsewhere it is the search too.
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: !shell.covered
        onActivated: {
            const songPage = stack.currentItem as SongCollectionPage;
            if (songPage && songPage.findable) {
                songPage.find();
            } else {
                shell.searchField.forceActiveFocus();
                shell.searchField.selectAll();
            }
        }
    }
    Shortcut {
        sequences: [StandardKey.Preferences, "Ctrl+,"]
        onActivated: {
            shell.uncoverRequested();
            shell.openSettings();
        }
    }

    // Playback, from anywhere (docs/ui/pages/settings.md §4). Text fields and lists
    // keep the keys they use themselves (Ctrl+←/→ in a field, Ctrl+↑/↓ in a list).
    Shortcut {
        sequence: "Ctrl+P"
        enabled: App.player.current.key !== ""
        onActivated: App.player.playPause()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        enabled: App.player.canPrevious
        onActivated: App.player.previous()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        enabled: App.player.canNext
        onActivated: App.player.next()
    }
    Shortcut {
        sequence: "Ctrl+Up"
        onActivated: shell.stepVolume(0.1)
    }
    Shortcut {
        sequence: "Ctrl+Down"
        onActivated: shell.stepVolume(-0.1)
    }
    // The player reports a new volume a moment later: quick presses step from the
    // last one asked for, not from the volume shown.
    property real pendingVolume: -1
    function stepVolume(delta: real): void {
        const from = pendingVolume >= 0 ? pendingVolume : App.player.volume;
        pendingVolume = Math.max(0, Math.min(1, Math.round((from + delta) * 10) / 10));
        App.player.setVolume(pendingVolume);
        volumeSettled.restart();
    }
    Timer {
        id: volumeSettled
        interval: 1000
        onTriggered: shell.pendingVolume = -1
    }
    Shortcut {
        sequence: "Ctrl+L"
        enabled: App.player.current.key !== "" && !App.player.current.voice
        onActivated: {
            if (!App.session.signedIn) {
                shell.openLogin();
                return;
            }
            const key = App.player.current.key;
            App.library.setLiked(key, !App.library.isLiked(key));
        }
    }
    Shortcut {
        sequences: [StandardKey.Back]
        enabled: !shell.covered && shell.canGoBack
        onActivated: shell.back()
    }
    Shortcut {
        sequences: [StandardKey.Forward]
        enabled: !shell.covered && shell.canGoForward
        onActivated: shell.forward()
    }
    TapHandler {
        enabled: !shell.covered
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        onTapped: (eventPoint, button) => {
            if (button === Qt.BackButton)
                shell.back();
            else
                shell.forward();
        }
    }

    AddToPlaylistDialog {
        id: addToPlaylistDialog
        library: myLibrary
    }

    function copyPageLink(): void {
        const item = pageShare;
        if (item)
            App.share.copy(item.kind, item.key, item.title, item.author);
    }

    // 已复制链接.
    Connections {
        target: App.share
        function onNotice(message: string) {
            shell.notify(message);
        }
    }

    // What App.player has to say (a song only plays as a preview).
    Connections {
        target: App.player
        function onNotice(message: string) {
            shell.notify(message);
        }
    }

    // What App.library has to say (liked, added to a playlist, failures).
    Connections {
        target: App.library
        function onNotice(message: string) {
            shell.notify(message);
        }
        function onPlaylistChanged() {
            shell.refreshLibrary();
        }
    }

    LoginDialog {
        id: loginDialog
    }

    // What App.together has to say (who joined or left, the room ended…).
    Connections {
        target: App.together
        function onNotice(message: string) {
            shell.notify(message);
        }
    }

    TogetherDialog {
        id: togetherDialog
    }

    Dialog {
        id: fmTogetherDialog
        title: qsTr("开始私人 FM？")
        supportingText: qsTr("开始私人 FM 会退出一起听。")
        acceptText: qsTr("开始")
        rejectText: qsTr("取消")
        onAccepted: {
            App.together.leave();
            App.player.startFm();
        }
    }
}
