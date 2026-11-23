pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The mini player docked as a pane on the right of the window (docs/ui/redesign-md3e.md
// §11), in the Material player page's language (pages/player-expressive.md): the pane
// is the cover's color (PrimaryContainer of the scheme the shell hands it) and all on it
// a tone of that hue. The cover (a 12-sided cookie turning slowly while playing, a
// rounded square while paused), the title
// and artist, a wavy seek bar, previous / play / next, a tray of connected buttons
// (shuffle, repeat, like, volume, the queue), then the lyrics or the songs up next
// (a switch, remembered). A click on the cover or the title opens the player page; the
// song changes with a shared-axis move. Its top strip lies under the window's title bar
// row: the shell puts the pane's own header buttons there (TopBar actions).
// For the player page's shared elements it offers the same items as MiniPlayer: the
// pane the page grows from (`bar`), the cover, title, transport buttons and seek bar.
// Laid out with anchors and implicit heights, not positioners: the player page measures
// these while the shell is hidden behind it, when every `visible` reads false.
Rectangle {
    id: root

    // The title bar row over the pane's top: the content starts under it.
    property real topInset: 64
    // While the player page's copies fly, the cover is hidden.
    property bool flying: false
    // The right corners, on the window's edge: the window's own radius.
    property real edgeRadius: 0

    readonly property Item bar: root
    readonly property Item coverItem: coverBox
    readonly property Item titleItem: texts
    readonly property Item controlsItem: transport
    readonly property Item seekItem: seek
    // The cover's shape and turn now, where the player page's flying cover starts.
    readonly property int coverShape: cover.shape
    readonly property real coverRotation: cover.rotation
    readonly property real coverRadius: cover.radius

    // The scheme the shell hands the pane (an attached property of the pane: inside
    // the QtObject it would read the app's).
    readonly property PlayerTones tones: PlayerTones {
        scheme: root.ColorScope.scheme
    }
    readonly property bool isSong: swap.song.key !== "" && !swap.song.voice
    readonly property bool playing: App.player.status === Playback.Playing
    readonly property bool loading: App.player.status === Playback.Loading
    readonly property real inner: width - 48
    readonly property bool lyricsShown: App.settings.sideSection === 0
    // The cover as large as leaves the lower part at least 220.
    readonly property real coverSize: Math.round(Math.max(120, Math.min(inner, height - topInset - 4
                                                                       - restHeight - 220)))
    readonly property real restHeight: 20 + texts.height + 16 + seekBox.height + 12 + transport.height
                                       + 16 + tray.height + 20 + sections.implicitHeight + 8 + 16

    signal openRequested
    signal queueRequested
    signal loginRequested

    // The focus comes back here when the player page closes.
    function takeFocus(): void {
        playButton.forceActiveFocus(Qt.OtherFocusReason);
    }

    color: tones.background
    radius: Theme.shape.extraLarge
    topRightRadius: edgeRadius
    bottomRightRadius: edgeRadius
    clip: true

    PlaybackClock {
        id: clock
        active: root.visible
    }
    // The lyrics follow this pane's clock while they show here (the player page drives
    // them while it is open, and the shell is hidden then).
    Binding {
        target: App.lyrics
        property: "positionMs"
        value: clock.positionMs
        when: root.visible && root.lyricsShown
    }
    SongSwap {
        id: swap
        animate: root.visible
    }

    // In the strip under the title bar row, level with the window buttons.
    Text {
        x: 24
        y: (root.topInset - height) / 2
        text: qsTr("正在播放")
        color: root.tones.ink
        font: Theme.type.titleSmall.font
    }

    // A 12-sided cookie turning slowly while playing, a rounded square while paused.
    Item {
        id: coverBox
        x: (root.width - width) / 2
        y: root.topInset + 4
        width: root.coverSize
        height: width

        ShapedCover {
            id: cover
            anchors.fill: parent
            opacity: (root.flying ? 0 : 1) * swap.fade
            coverKey: swap.song.coverKey
            playing: root.playing
            transform: Translate {
                x: swap.offset * 32
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.openRequested()
        }
        TogetherBadge {
            x: parent.width - width - 4
            y: parent.height - height - 4
            size: 32
            opacity: cover.opacity
        }
    }

    Item {
        id: texts
        x: 24
        anchors.top: coverBox.bottom
        anchors.topMargin: 20
        width: root.inner
        height: titleText.implicitHeight + 2 + artistLine.implicitHeight
        opacity: swap.fade
        transform: Translate {
            x: swap.offset * 16
        }

        Text {
            id: titleText
            width: parent.width
            text: swap.song.title
            elide: Text.ElideRight
            color: root.tones.ink
            font: Fonts.font(600, 20)
        }
        Text {
            id: artistLine
            readonly property bool failed: App.player.status === Playback.Failed
            y: titleText.implicitHeight + 2
            width: parent.width
            text: failed ? App.player.failureMessage : swap.song.artistText
            elide: Text.ElideRight
            color: failed ? ColorScope.scheme.error : root.tones.inkVariant
            font: Theme.type.bodyLarge.font
        }
        TapHandler {
            onTapped: root.openRequested()
        }
        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }

    // The seek bar and the times under it.
    Item {
        id: seekBox
        x: 24
        anchors.top: texts.bottom
        anchors.topMargin: 16
        width: root.inner
        height: seek.implicitHeight + elapsed.implicitHeight

        WavySeek {
            id: seek
            width: parent.width
            tones: root.tones
            trackHeight: 4
            handleHeight: 20
            roundHandle: true
            dotSize: 14
            waveAmplitude: 3
            waveLength: 32
            waveSpeed: 16
            focusPolicy: Qt.TabFocus
            positionMs: clock.positionMs
            playing: root.playing && App.settings.miniWave
        }
        Text {
            id: elapsed
            anchors.top: seek.bottom
            text: App.format.duration(seek.shownMs)
            color: root.tones.inkVariant
            font: Fonts.tabular(Theme.type.labelMedium.font)
        }
        Text {
            anchors.top: seek.bottom
            anchors.right: parent.right
            text: App.player.durationMs <= 0 ? "--:--"
                  : App.settings.playerTimeRight === 1 ? App.format.duration(App.player.durationMs)
                  : "-" + App.format.duration(App.player.durationMs - seek.shownMs)
            color: root.tones.inkVariant
            font: Fonts.tabular(Theme.type.labelMedium.font)
        }
    }

    // Previous, play or pause, next: pills a tone toward the ink and the play button in
    // Primary (square while playing, round while paused).
    Item {
        id: transport
        readonly property real spacing: 8
        readonly property real side: Math.round((width - 2 * spacing) / 3.4)
        x: 24
        anchors.top: seekBox.bottom
        anchors.topMargin: 12
        width: root.inner
        height: playButton.implicitHeight

        IconButton {
            width: transport.side
            size: Md.Medium
            containerTint: root.tones.raised
            tint: root.tones.ink
            enabled: App.player.canPrevious
            glyph: Symbols.skip_previous
            iconFill: 1
            text: qsTr("上一首")
            onClicked: App.player.previous()
        }
        IconButton {
            id: playButton
            x: transport.side + transport.spacing
            width: transport.width - 2 * transport.side - 2 * transport.spacing
            variant: Md.Filled
            size: Md.Medium
            shape: root.playing || root.loading ? Md.Square : Md.Round
            glyph: root.loading ? "" : root.playing ? Symbols.pause : Symbols.play_arrow
            iconFill: 1
            text: root.loading ? qsTr("正在加载") : root.playing ? qsTr("暂停") : qsTr("播放")
            onClicked: App.player.playPause()

            LoadingIndicator {
                anchors.centerIn: parent
                implicitWidth: 32
                implicitHeight: 32
                visible: root.loading
                color: ColorScope.scheme.onPrimary
            }
        }
        IconButton {
            x: transport.width - width
            width: transport.side
            size: Md.Medium
            containerTint: root.tones.raised
            tint: root.tones.ink
            enabled: App.player.canNext
            glyph: Symbols.skip_next
            iconFill: 1
            text: qsTr("下一首")
            onClicked: App.player.next()
        }
    }

    // Shuffle, repeat, like, the volume and the queue: connected buttons on a pill a
    // tone away from the pane.
    Rectangle {
        id: tray
        x: 24
        anchors.top: transport.bottom
        anchors.topMargin: 16
        width: root.inner
        height: trayGroup.implicitHeight + 8
        radius: height / 2
        color: root.tones.tray

        component TrayButton: IconButton {
            width: trayGroup.itemWidth
            containerTint: root.tones.trayButton
            tint: root.tones.ink
        }

        ConnectedButtonGroup {
            id: trayGroup
            readonly property int count: root.isSong ? 5 : 4
            readonly property real itemWidth: (width - (count - 1) * spacing) / count
            x: 4
            y: 4
            width: parent.width - 8

            TrayButton {
                id: shuffleButton
                readonly property bool on: App.player.order === Playback.Shuffle
                checkable: true
                checked: on
                glyph: Symbols.shuffle
                text: on ? qsTr("关闭随机播放") : qsTr("随机播放")
                onClicked: {
                    checked = Qt.binding(() => shuffleButton.on);
                    App.player.setOrder(on ? Playback.Sequential : Playback.Shuffle);
                }
            }
            TrayButton {
                id: repeatButton
                readonly property int repeat: App.player.repeat
                checkable: true
                checked: repeat !== Playback.RepeatOff
                glyph: repeat === Playback.RepeatOne ? Symbols.repeat_one : Symbols.repeat
                text: repeat === Playback.RepeatAll ? qsTr("列表循环")
                    : repeat === Playback.RepeatOne ? qsTr("单曲循环") : qsTr("不循环")
                onClicked: {
                    checked = Qt.binding(() => repeatButton.repeat !== Playback.RepeatOff);
                    App.player.setRepeat(repeat === Playback.RepeatAll ? Playback.RepeatOne
                                       : repeat === Playback.RepeatOne ? Playback.RepeatOff
                                       : Playback.RepeatAll);
                }
            }
            TrayButton {
                id: likeButton
                readonly property string key: App.player.current.key
                readonly property bool liked: App.library.likedRevision >= 0 && App.library.isLiked(key)
                visible: root.isSong
                checkable: true
                checked: liked
                glyph: Symbols.favorite
                iconFill: liked ? 1 : 0
                pops: true
                text: liked ? qsTr("取消喜欢") : qsTr("喜欢")
                onClicked: {
                    checked = Qt.binding(() => likeButton.liked);
                    if (!App.session.signedIn)
                        root.loginRequested();
                    else
                        App.library.setLiked(key, !liked);
                }
            }
            MiniVolume {
                width: trayGroup.itemWidth
                containerTint: root.tones.trayButton
                tint: root.tones.ink
            }
            TrayButton {
                glyph: Symbols.queue_music
                text: qsTr("播放队列")
                onClicked: root.queueRequested()
            }
        }
    }

    // The lyrics or the songs up next.
    ConnectedButtonGroup {
        id: sections
        x: 24
        anchors.top: tray.bottom
        anchors.topMargin: 20
        width: root.inner

        Button {
            width: (sections.width - sections.spacing) / 2
            containerTint: root.tones.tray
            tint: root.tones.ink
            checkable: true
            checked: root.lyricsShown
            text: qsTr("歌词")
            onClicked: {
                checked = Qt.binding(() => root.lyricsShown);
                App.settings.sideSection = 0;
            }
        }
        Button {
            width: (sections.width - sections.spacing) / 2
            containerTint: root.tones.tray
            tint: root.tones.ink
            checkable: true
            checked: !root.lyricsShown
            text: qsTr("接下来")
            onClicked: {
                checked = Qt.binding(() => !root.lyricsShown);
                App.settings.sideSection = 1;
            }
        }
    }

    Item {
        id: lower
        x: 24
        anchors.top: sections.bottom
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: root.inner

        SideLyrics {
            anchors.fill: parent
            visible: root.lyricsShown
            tones: root.tones
        }

        // The next songs of the queue, as many as fit; a click plays one.
        ListView {
            id: nextList
            readonly property int first: App.player.queueIndex + 1
            readonly property int shown: Math.max(0, Math.min(Math.floor((lower.height + 2) / 58),
                                                              App.player.queue.count - first))
            function load(): void {
                if (shown > 0)
                    App.player.ensureQueueLoaded(first, first + shown - 1);
            }
            function align(): void {
                if (first < count)
                    positionViewAtIndex(first, ListView.Beginning);
            }
            width: parent.width
            height: Math.max(0, shown * 58 - 2)
            visible: !root.lyricsShown
            interactive: false
            clip: true
            spacing: 2
            model: App.player.queue
            onFirstChanged: {
                Qt.callLater(load);
                Qt.callLater(align);
            }
            onShownChanged: Qt.callLater(load)
            onCountChanged: Qt.callLater(align)
            onHeightChanged: Qt.callLater(align)
            onVisibleChanged: if (visible)
                                  Qt.callLater(align)

            delegate: ListItem {
                id: row
                required property int index
                required property string title
                required property string artistText
                required property string coverKey
                readonly property int place: index - nextList.first
                width: nextList.width
                height: 56
                segmented: true
                first: place === 0
                last: place === nextList.shown - 1
                containerColor: root.tones.raised
                text: row.title
                supportingText: row.artistText
                onClicked: App.player.playAt(row.index)
                leading: Component {
                    CoverArt {
                        width: 40
                        height: 40
                        radius: Theme.shape.small
                        coverKey: row.coverKey
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: !root.lyricsShown && nextList.shown === 0
            text: qsTr("没有接下来的歌曲")
            color: root.tones.inkVariant
            font: Theme.type.titleMedium.font
        }
    }
}
