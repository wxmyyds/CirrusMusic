pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The persistent mini player: a floating toolbar over the bottom of the content panel
// with a button beside it that docks the player as a side pane instead (SidePlayer,
// shown where there is room; docs/ui/redesign-md3e.md §8, §11). A click on the bar's
// free space opens the player page.
// The MD3E floating toolbar in its vibrant colors (androidx FloatingToolbarTokens): 64
// high, fully round, PrimaryContainer, 16 from the edge, 8 inside each end, the FAB 8
// beside it. One row: the cover and title, the like, a seek bar that waves while
// playing (the times at its ends), the transport buttons, then the play mode, volume,
// queue and comments. Narrower bars drop the extras and the times, then trade the seek
// bar (and "previous") for a progress ring around the play button (`tier`). It slides
// in once something is queued. Pages scroll under it: the shell tells them how much it
// covers (`reservedHeight`, PageInsets).
Item {
    id: root

    readonly property bool active: App.player.status !== Playback.Empty
    // From the bottom edge of the area it floats over.
    property real bottomMargin: 16
    // Out of the way (the player docked as a side pane): it slides away.
    property bool hidden: false
    // There is room to dock it as a side pane: the button beside it offers that.
    property bool canDock: true
    // What the bar covers at the bottom, with a little air above it.
    readonly property real reservedHeight: (implicitHeight + bottomMargin + 8) * reveal.value
    // 960 on ordinary windows; over wide content it grows by half of what is past 1400,
    // up to 1240 (the seek bar gets the room).
    property real maximumWidth: Math.round(Math.min(1240, 960 + Math.max(0, width - 1400) / 2))
    readonly property real barWidth: Math.max(0, Math.min(maximumWidth, width - 32
                                                          - (canDock ? fab.width + 8 : 0)))
    // 2 wide: the seek bar with the times, and the extras; 1 medium: a shorter seek bar,
    // the time over the handle while dragging; 0 narrow: no seek bar, no "previous", a
    // progress ring around the play button.
    // The thresholds leave the title at least `titleRoom` beside a seek bar of a usable
    // length; each button the user turned off (MiniPlayerButtonsMenu) lowers them by its
    // width, so a smaller bar gets the full layout.
    readonly property var hiddenButtons: App.settings.miniPlayerHidden
    readonly property bool likeOn: !hiddenButtons.includes("like")
    readonly property bool modeOn: !hiddenButtons.includes("mode")
    readonly property bool volumeOn: !hiddenButtons.includes("volume")
    readonly property bool queueOn: !hiddenButtons.includes("queue")
    readonly property bool commentsOn: !hiddenButtons.includes("comments")
    readonly property real buttonRoom: 40
    readonly property real savedMedium: ((likeOn ? 0 : 1) + (commentsOn ? 0 : 1)) * buttonRoom
    readonly property real savedWide: savedMedium
                                      + ((modeOn ? 0 : 1) + (volumeOn ? 0 : 1) + (queueOn ? 0 : 1))
                                      * buttonRoom
    readonly property int tier: barWidth >= 880 - savedWide ? 2
                              : barWidth >= 620 - savedMedium ? 1 : 0
    readonly property real titleRoom: 160
    readonly property bool wide: tier > 0
    // Layout decisions read these, not `visible`: the page captures this layout while
    // the shell is hidden behind it, when every `visible` here reads false.
    readonly property bool isSong: App.player.current.key !== "" && !App.player.current.voice
    readonly property bool hasComments: isSong && commentsOn
    readonly property bool showLike: tier > 0 && isSong && likeOn
    readonly property bool showExtras: tier === 2
    readonly property color content: ColorScope.scheme.onPrimaryContainer
    // For the player page's shared elements: the card it grows from, and the cover,
    // title, buttons and seek bar that fly to their places on the page. While `flying`
    // the cover and the times are hidden (the page draws copies of the rest).
    readonly property Item bar: card
    readonly property Item coverItem: cover
    readonly property Item titleItem: texts
    readonly property Item controlsItem: controls
    readonly property Item seekItem: tier > 0 ? seek : null
    property bool flying: false

    // Opens the player page (UI phase 4).
    signal openRequested
    // The player page with its queue panel.
    signal queueRequested
    // The player as a pane on the right instead (SidePlayer).
    signal dockRequested
    // The current song's comments (not for voices).
    signal commentsRequested
    // Liking needs the sign-in first.
    signal loginRequested

    // The focus comes back here when the player page closes: its play button.
    function takeFocus(): void {
        playButton.forceActiveFocus(Qt.OtherFocusReason);
    }

    readonly property real timeWidth: 44
    implicitHeight: 64
    visible: reveal.value > 0

    Spring {
        id: reveal
        target: root.active && !root.hidden ? 1 : 0
        spec: Theme.motion.standardDefaultSpatial
        threshold: 0.001
        Component.onCompleted: snapTo(target)
    }

    PlaybackClock {
        id: clock
        active: root.visible
    }

    // The bar and the button, centred; they slide up from below the edge.
    Item {
        id: group
        x: Math.round((root.width - width) / 2)
        y: (1 - reveal.value) * (height + root.bottomMargin + 16)
        width: root.barWidth + (root.canDock ? 8 + fab.width : 0)
        height: root.implicitHeight

        Elevation {
            anchors.fill: card
            level: Theme.elevation.level1
            radius: card.radius
        }

        Rectangle {
            id: card
            width: root.barWidth
            height: parent.height
            radius: height / 2
            color: ColorScope.scheme.primaryContainer

            // A click anywhere free opens the player page. A MouseArea, not a TapHandler:
            // it takes the press, so what floats under the bar (a song table's rows) is
            // not clicked or hovered through it.
            MouseArea {
                id: cardArea
                // Over the bar itself, the cover or the title, not over a button.
                property bool free: false
                function update(): void {
                    const at = card.childAt(mouseX, mouseY);
                    free = containsMouse && (at === null || at === cardArea || at === cover
                                             || at === texts);
                }
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                // Right: which buttons show.
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPositionChanged: update()
                onContainsMouseChanged: update()
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton)
                        buttonsMenu.popup(cardArea, mouse.x, mouse.y);
                    else
                        root.openRequested();
                }
            }
            MiniPlayerButtonsMenu {
                id: buttonsMenu
            }
            HoverHandler {
                blocking: true
            }
            StateLayer {
                anchors.fill: parent
                radius: card.radius
                color: root.content
                // Hover only: a press opens the player page, which grows out of the bar
                // at once, so a ripple would only be seen under it.
                hovered: cardArea.free
            }

            CoverArt {
                id: cover
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                height: 48
                radius: 24
                coverKey: App.player.current.coverKey
                opacity: root.flying ? 0 : 1
            }
            TogetherBadge {
                x: cover.x + cover.width - width + 4
                y: cover.y + cover.height - height + 4
                opacity: cover.opacity
            }

            // Implicit heights, not a Column: the player page measures this while the
            // shell is hidden, when a Column of "invisible" lines would collapse.
            Item {
                id: texts
                height: miniTitle.implicitHeight + 2 + miniArtist.implicitHeight
                anchors.left: cover.right
                anchors.leftMargin: 12
                anchors.right: root.tier === 0 ? controls.left : root.showLike ? likeButton.left
                                                                               : center.left
                anchors.rightMargin: root.showLike ? 8 : 16
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    id: miniTitle
                    width: parent.width
                    text: App.player.current.title
                    elide: Text.ElideRight
                    color: root.content
                    font: Theme.type.titleSmall.font
                }
                Text {
                    id: miniArtist
                    readonly property bool failed: App.player.status === Playback.Failed
                    y: miniTitle.implicitHeight + 2
                    width: parent.width
                    text: failed ? App.player.failureMessage : App.player.current.artistText
                    elide: Text.ElideRight
                    color: failed ? ColorScope.scheme.error : Qt.alpha(root.content, 0.8)
                    font: Theme.type.bodyMedium.font
                }
            }

            IconButton {
                id: likeButton
                readonly property string key: App.player.current.key
                readonly property bool liked: App.library.likedRevision >= 0 && App.library.isLiked(key)
                anchors.right: center.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: root.showLike
                tint: root.content
                glyph: Symbols.favorite
                iconFill: liked ? 1 : 0
                pops: true
                text: liked ? qsTr("取消喜欢") : qsTr("喜欢")
                onClicked: {
                    if (!App.session.signedIn)
                        root.loginRequested();
                    else
                        App.library.setLiked(key, !liked);
                }
            }

            // The seek bar between the title and the buttons, the times at its ends on
            // wide bars; narrow bars have none (the play button's ring instead).
            Item {
                id: center
                anchors.right: controls.left
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                visible: root.tier > 0
                // Grows with the bar (42% of it) on wide bars, as long as the title keeps
                // its room; at most 280 on medium ones.
                readonly property real room: controls.x - 16
                                             - (cover.x + cover.width + 12 + root.titleRoom + 16
                                                + (root.showLike ? likeButton.width : 0))
                width: Math.max(0, root.tier === 2 ? Math.min(root.barWidth * 0.42, room)
                                                   : Math.min(280, room))
                height: seek.height

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: seek.verticalCenter
                    visible: root.tier === 2
                    opacity: root.flying ? 0 : 1
                    // Back after the flight without popping in.
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.motion.fastEffects.duration
                        }
                    }
                    text: App.format.duration(seek.shownMs)
                    color: Qt.alpha(root.content, 0.8)
                    font: Fonts.tabular(Theme.type.labelSmall.font)
                }

                // The seek bar: a wave while playing (a setting), a line while paused,
                // and a short bar to drag. Dragging only previews; the seek happens on
                // release and the handle stays there until the player has caught up.
                Slider {
                    id: seek

                    // The position held after a seek until the player reports it; else -1.
                    property real heldMs: -1
                    readonly property real shownMs: pressed ? valueAt(position) : value

                    function seekTo(ms: real): void {
                        heldMs = ms;
                        App.player.seek(ms);
                        caughtUp.restart();
                    }

                    anchors.left: parent.left
                    anchors.leftMargin: root.tier === 2 ? root.timeWidth : 0
                    anchors.right: parent.right
                    anchors.rightMargin: root.tier === 2 ? root.timeWidth : 0
                    anchors.verticalCenter: parent.verticalCenter
                    trackHeight: 4
                    handleHeight: 16
                    waveSpeed: 16
                    roundHandle: true
                    inactiveTint: Qt.alpha(root.content, 0.3)
                    wavy: App.settings.miniWave
                    waving: App.player.status === Playback.Playing && !pressed
                    enabled: App.player.seekable && App.player.durationMs > 0
                    opacity: App.player.durationMs > 0 ? 1 : 0
                    live: false
                    // Without the times: the time it would go to, over the handle while dragging.
                    showValueLabel: root.tier === 1
                    valueText: App.format.duration(shownMs)
                    focusPolicy: Qt.TabFocus
                    from: 0
                    to: Math.max(1, App.player.durationMs)
                    stepSize: 5000
                    // Bounded by `to` so it is evaluated again when `to` changes: a restored
                    // position arriving with its duration may be evaluated before `to` and be
                    // clamped to the old one (the control clamps `value`), and nothing would
                    // set it again.
                    value: Math.min(to, pressed ? value : heldMs >= 0 ? heldMs : clock.positionMs)
                    Accessible.name: qsTr("播放进度")
                    onPressedChanged: if (!pressed)
                                          seekTo(valueAt(position))
                    onMoved: if (!pressed)
                                 seekTo(value)

                    Connections {
                        target: App.player
                        function onChanged() {
                            if (!seek.pressed)
                                seek.heldMs = -1;
                        }
                    }
                    Timer {
                        id: caughtUp
                        interval: 1500
                        onTriggered: seek.heldMs = -1
                    }
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: seek.verticalCenter
                    visible: root.tier === 2
                    opacity: root.flying ? 0 : 1
                    // Back after the flight without popping in.
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.motion.fastEffects.duration
                        }
                    }
                    horizontalAlignment: Text.AlignRight
                    text: App.player.durationMs <= 0 ? "--:--"
                          : App.settings.playerTimeRight === 1 ? App.format.duration(App.player.durationMs)
                          : "-" + App.format.duration(App.player.durationMs - seek.shownMs)
                    color: Qt.alpha(root.content, 0.8)
                    font: Fonts.tabular(Theme.type.labelSmall.font)
                }
            }

            // Pressing a button widens it and squeezes its neighbours (MD3E button
            // group). The play button is wide, square while playing and round while
            // paused, and spins a loading indicator while the song loads.
            StandardButtonGroup {
                id: controls
                anchors.right: trailing.left
                anchors.rightMargin: trailing.width > 0 ? 4 : 0
                anchors.verticalCenter: parent.verticalCenter

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.wide
                    enabled: App.player.canPrevious
                    tint: root.content
                    glyph: Symbols.skip_previous
                    iconFill: 1
                    text: qsTr("上一首")
                    onClicked: App.player.previous()
                }
                IconButton {
                    id: playButton
                    readonly property bool loading: App.player.status === Playback.Loading
                    readonly property bool playing: App.player.status === Playback.Playing || loading
                    anchors.verticalCenter: parent.verticalCenter
                    variant: Md.Filled
                    widthMode: root.tier > 0 ? Md.Wide : Md.Default
                    // Round inside the ring on narrow bars.
                    shape: playing && root.tier > 0 ? Md.Square : Md.Round
                    glyph: loading ? "" : playing ? Symbols.pause : Symbols.play_arrow
                    iconFill: 1
                    text: loading ? qsTr("正在加载") : playing ? qsTr("暂停") : qsTr("播放")
                    onClicked: App.player.playPause()

                    // Narrow bars have no seek bar: a ring around the button shows the
                    // progress.
                    CircularProgress {
                        anchors.centerIn: parent
                        width: parent.height + 12
                        height: width
                        thickness: 3
                        visible: root.tier === 0 && App.player.durationMs > 0
                        from: 0
                        to: Math.max(1, App.player.durationMs)
                        // Bounded by `to` so it is evaluated again when `to` changes, see `seek`.
                        value: Math.min(to, clock.positionMs)
                    }
                    LoadingIndicator {
                        anchors.centerIn: parent
                        implicitWidth: 28
                        implicitHeight: 28
                        visible: playButton.loading
                        color: ColorScope.scheme.onPrimary
                    }
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: App.player.canNext
                    tint: root.content
                    glyph: Symbols.skip_next
                    iconFill: 1
                    text: qsTr("下一首")
                    onClicked: App.player.next()
                }
            }

            Row {
                id: trailing
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter

                // The play mode, one button cycling as the player page's does
                // (App.player.playMode, ui/player/PlayerControls.qml).
                IconButton {
                    id: modeButton
                    visible: root.showExtras && root.modeOn
                    tint: root.content
                    glyph: [Symbols.repeat, Symbols.repeat_one, Symbols.shuffle,
                        Symbols.playlist_play, Symbols.ecg_heart][App.player.playMode]
                    text: [qsTr("列表循环"), qsTr("单曲循环"), qsTr("随机播放"),
                        qsTr("顺序播放"), qsTr("心动模式")][App.player.playMode]
                    onClicked: App.player.cyclePlayMode()

                    CircularProgress {
                        anchors.centerIn: parent
                        width: 34
                        height: width
                        thickness: 2
                        indeterminate: true
                        visible: App.player.heartbeatLoading
                    }
                }
                MiniVolume {
                    visible: root.showExtras && root.volumeOn
                    tint: root.content
                }
                IconButton {
                    visible: root.showExtras && root.queueOn
                    tint: root.content
                    glyph: Symbols.queue_music
                    text: qsTr("播放队列")
                    onClicked: root.queueRequested()
                }
                IconButton {
                    visible: root.hasComments
                    tint: root.content
                    glyph: Symbols.comment
                    text: qsTr("评论")
                    onClicked: root.commentsRequested()
                }
            }
        }

        // Beside a floating toolbar the FAB sits one elevation level lower than alone
        // (androidx FloatingToolbarDefaults), in the tertiary role next to a vibrant bar.
        Fab {
            id: fab
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: root.canDock
            tone: Md.Tertiary
            restElevation: Theme.elevation.level2
            hoverElevation: Theme.elevation.level3
            glyph: Symbols.right_panel_open
            text: qsTr("转为侧边播放器")
            onClicked: root.dockRequested()
        }
    }
}
