pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation
import Cirrus.Window

// The player page in the Material style (docs/ui/pages/player-expressive.md): the whole
// page one color from the cover (PrimaryContainer; PlayerPage paints it) and everything
// on it a tone of the same hue. A top bar (collapse, the views as connected buttons, the
// song menu), the now-playing column and, with lyrics, the lyrics straight on the color
// beside it. Narrow windows with lyrics show the lyrics alone over a seek pill. The
// column glides when lyrics are shown or hidden. PlayerPage owns the opening
// transition, the side panels and the keys; this item only lays out and asks.
Item {
    id: root

    required property PlayerPalette colors
    required property AppWindow appWindow
    property int view: 0
    property int sheet: 0
    property real positionMs: 0
    property bool flying: false
    property bool hasSettings: true
    // Layout changes glide (off while the page opens or the window resizes).
    property bool animate: false

    readonly property PlayerTones tones: PlayerTones {
        scheme: root.colors.scheme
    }
    readonly property bool wide: width >= 900
    readonly property bool lyrics: view === 1
    readonly property bool split: wide && lyrics
    readonly property bool lyricsOnly: !wide && lyrics
    readonly property bool playing: App.player.status === Playback.Playing
    readonly property bool loading: App.player.status === Playback.Loading

    // What PlayerPage reads for the cover's flight, the focus and the side panels.
    readonly property Item coverItem: column.coverItem
    readonly property int coverShape: -1
    readonly property real coverRotation: 0
    readonly property real coverRadius: column.coverRadius
    readonly property Item playButton: lyricsOnly ? pillPlay : column.playButton
    readonly property Item queueButton: queueToggle
    readonly property Item settingsButton: settingsToggle
    readonly property real contentTop: 72
    readonly property real sheetBottom: height - 24
    readonly property color sheetColor: tones.tray

    // The column's area (the left 46% beside lyrics, else the page), width and place.
    readonly property real area: split ? Math.round(width * 0.46) : width
    readonly property real columnWidth: Math.min(440, area - (split ? 64 : 48))
    readonly property real coverSize: Math.round(Math.max(200, Math.min(columnWidth,
                                          height - contentTop - 24 - column.restHeight)))
    readonly property real columnLeft: Math.round((area - columnWidth) / 2)
    readonly property real columnTop: Math.round(contentTop + Math.max(0, (height - contentTop - 24
                                                                           - column.implicitHeight) / 2))

    signal closeRequested
    signal fullScreenRequested
    signal viewRequested(int view)
    signal sheetRequested(int sheet)
    signal artistRequested(string key)
    signal albumRequested(string key)
    signal podcastRequested(string key) // a voice's podcast
    signal songRequested
    signal commentsRequested
    signal addToPlaylistRequested(string key, string title)
    signal copyRequested
    signal loginRequested
    signal togetherRequested // listen together's dialog (pages/together.md §1)

    // A spring that follows `to`, or jumps there while the layout doesn't animate.
    component Follow: Spring {
        property real to
        spec: Theme.motion.defaultSpatial
        onToChanged: root.animate ? target = to : snapTo(to)
        Component.onCompleted: snapTo(to)
    }

    // The top bar; moves the window like the shell's title bar row.
    TitleBar {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.contentTop
        contentHeight: root.contentTop
        frame: root.appWindow.frame
        windowControls: root.appWindow.titleBar !== TitleBarMode.System
        windowButtons: root.appWindow.customFrame && root.appWindow.visibility !== Window.FullScreen
        buttonTint: root.tones.ink
        leadingInset: root.appWindow.titleBar === TitleBarMode.Custom && root.appWindow.macOS ? 72 : 16

        IconButton {
            id: closeButton
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            containerTint: root.tones.tray
            tint: root.tones.ink
            glyph: Symbols.expand_more
            text: qsTr("关闭播放页")
            onClicked: root.closeRequested()
        }
        Text {
            anchors.left: closeButton.right
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("正在播放")
            color: root.tones.ink
            font: Theme.type.titleLarge.font
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            // The views and panels: connected toggles, selected in Primary.
            ConnectedButtonGroup {
                anchors.verticalCenter: parent.verticalCenter

                component ViewToggle: IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    widthMode: Md.Wide
                    containerTint: root.tones.tray
                    tint: root.tones.ink
                    checkable: true
                }
                ViewToggle {
                    checked: root.lyrics
                    glyph: Symbols.lyrics
                    text: qsTr("歌词")
                    onClicked: {
                        checked = Qt.binding(() => root.lyrics);
                        root.viewRequested(root.lyrics ? 0 : 1);
                    }
                }
                ViewToggle {
                    id: queueToggle
                    checked: root.sheet === 1
                    glyph: Symbols.queue_music
                    text: qsTr("播放队列")
                    onClicked: {
                        checked = Qt.binding(() => root.sheet === 1);
                        root.sheetRequested(root.sheet === 1 ? 0 : 1);
                    }
                }
                ViewToggle {
                    id: settingsToggle
                    visible: root.hasSettings
                    checked: root.sheet === 2
                    glyph: Symbols.tune
                    text: qsTr("显示设置")
                    onClicked: {
                        checked = Qt.binding(() => root.sheet === 2);
                        root.sheetRequested(root.sheet === 2 ? 0 : 2);
                    }
                }
            }
            // Listen together: a button; in a room, what it is (pages/together.md §4).
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: App.together.available && App.together.state === TogetherState.Idle
                containerTint: root.tones.tray
                tint: root.tones.ink
                glyph: Symbols.group
                text: qsTr("一起听")
                onClicked: root.togetherRequested()
            }
            Button {
                anchors.verticalCenter: parent.verticalCenter
                visible: App.together.available && App.together.state !== TogetherState.Idle
                containerTint: root.tones.tray
                tint: root.tones.ink
                iconFill: 1
                glyph: App.together.active && !App.together.connected ? Symbols.sync_problem
                                                                      : Symbols.group
                text: !App.together.active ? qsTr("一起听…")
                      : !App.together.connected ? qsTr("重新连接…")
                      : App.together.partnerName !== "" ? qsTr("和 %1").arg(App.together.partnerName)
                      : qsTr("等好友加入")
                onClicked: root.togetherRequested()
            }
            IconButton {
                id: moreButton
                anchors.verticalCenter: parent.verticalCenter
                containerTint: root.tones.tray
                tint: root.tones.ink
                glyph: Symbols.more_vert
                text: qsTr("更多")
                onClicked: songMenu.popup(moreButton, 0, moreButton.height + 4)
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                containerTint: root.tones.tray
                tint: root.tones.ink
                glyph: root.appWindow.visibility === Window.FullScreen ? Symbols.fullscreen_exit
                                                                       : Symbols.fullscreen
                text: root.appWindow.visibility === Window.FullScreen ? qsTr("退出全屏") : qsTr("全屏")
                onClicked: root.fullScreenRequested()
            }
        }
    }

    // The lyrics, straight on the page's color (the same settings as the default style).
    LyricsView {
        x: root.split ? root.area + 16 : 32
        y: root.contentTop
        width: root.split ? root.width - x - 48 : root.width - 64
        height: root.height - y - (root.split ? 24 : 24 + 72 + 16)
        colors: root.colors
        positionMs: root.positionMs
        live: root.visible // the page's, not the fade's
        opacity: lyricsFade.value
        visible: opacity > 0.01
        onCopyRequested: root.copyRequested()

        Spring {
            id: lyricsFade
            target: root.lyrics ? 1 : 0
            spec: Theme.motion.defaultEffects
            Component.onCompleted: snapTo(target)
        }
    }

    ExpressiveNowPlaying {
        id: column
        x: glideX.value
        y: glideY.value
        width: root.columnWidth
        tones: root.tones
        coverSize: root.coverSize
        positionMs: root.positionMs
        flying: root.flying
        opacity: columnFade.value
        visible: opacity > 0.01
        enabled: !root.lyricsOnly
        onArtistRequested: key => root.artistRequested(key)
        onSongRequested: root.songRequested()
        onLoginRequested: root.loginRequested()

        Follow {
            id: glideX
            to: root.columnLeft
        }
        Follow {
            id: glideY
            to: root.columnTop
        }
        Spring {
            id: columnFade
            target: root.lyricsOnly ? 0 : 1
            spec: Theme.motion.defaultEffects
            Component.onCompleted: snapTo(target)
        }
    }

    // Narrow windows with lyrics: play, the times and the seek bar, next, on a pill.
    Rectangle {
        id: pill
        x: 24
        y: root.height - 24 - height
        width: root.width - 48
        height: 72
        radius: height / 2
        color: root.tones.tray
        opacity: 1 - columnFade.value
        visible: opacity > 0.01
        enabled: root.lyricsOnly

        IconButton {
            id: pillPlay
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            variant: Md.Filled
            size: Md.Medium
            shape: root.playing || root.loading ? Md.Square : Md.Round
            glyph: root.playing || root.loading ? Symbols.pause : Symbols.play_arrow
            iconFill: 1
            text: root.playing ? qsTr("暂停") : qsTr("播放")
            onClicked: App.player.playPause()
        }
        Text {
            id: pillElapsed
            anchors.left: pillPlay.right
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: App.format.duration(pillSeek.shownMs)
            color: root.tones.inkVariant
            font: Fonts.tabular(Theme.type.labelLarge.font)
        }
        WavySeek {
            id: pillSeek
            anchors.left: pillElapsed.right
            anchors.leftMargin: 12
            anchors.right: pillTotal.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            tones: root.tones
            positionMs: root.positionMs
            playing: root.playing
        }
        Text {
            id: pillTotal
            anchors.right: pillNext.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: App.player.durationMs <= 0 ? "--:--" : App.format.duration(App.player.durationMs)
            color: root.tones.inkVariant
            font: Fonts.tabular(Theme.type.labelLarge.font)
        }
        IconButton {
            id: pillNext
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            size: Md.Medium
            containerTint: root.tones.trayButton
            tint: root.tones.ink
            enabled: App.player.canNext
            glyph: Symbols.skip_next
            iconFill: 1
            text: qsTr("下一首")
            onClicked: App.player.next()
        }
    }

    Menu {
        id: songMenu
        MenuItem {
            text: App.player.current.voice ? qsTr("查看播客") : qsTr("查看专辑")
            glyph: App.player.current.voice ? Symbols.podcasts : Symbols.album
            enabled: App.player.current.albumKey !== ""
            onTriggered: App.player.current.voice
                         ? root.podcastRequested(App.player.current.albumKey)
                         : root.albumRequested(App.player.current.albumKey)
        }
        MenuItem {
            text: qsTr("歌曲详情")
            glyph: Symbols.info
            enabled: !App.player.current.voice
            onTriggered: root.songRequested()
        }
        MenuItem {
            text: qsTr("评论")
            glyph: Symbols.comment
            enabled: !App.player.current.voice
            onTriggered: root.commentsRequested()
        }
        MenuItem {
            text: qsTr("加入歌单")
            glyph: Symbols.playlist_add
            enabled: !App.player.current.voice
            onTriggered: root.addToPlaylistRequested(App.player.current.key, App.player.current.title)
        }
        MenuItem {
            text: qsTr("复制链接")
            glyph: Symbols.link
            enabled: App.player.current.key !== ""
            onTriggered: App.share.copy(App.player.current.voice ? Share.Voice : Share.Song,
                                        App.player.current.key, App.player.current.title,
                                        App.player.current.artistText)
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
}
