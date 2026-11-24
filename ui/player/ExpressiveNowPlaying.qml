pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The Material player page's now-playing column (docs/ui/pages/player-expressive.md
// §2.1), straight on the page's color: a quiet rounded cover that shrinks and rounds
// while paused, the title and artist centered, a thick wavy seek bar with the times,
// previous / play / next as an MD3E standard button group of Large buttons, and a tray
// of connected buttons (shuffle, repeat, like, volume). Laid out with anchors and
// implicit heights only: PlayerPage measures the cover for its flight while hidden.
Item {
    id: root

    required property PlayerTones tones
    property real coverSize: 400
    property real positionMs: 0
    // While the player page's cover flies, this one is hidden.
    property bool flying: false

    readonly property Item coverItem: cover
    readonly property real coverRadius: cover.radius
    readonly property Item playButton: playButton
    readonly property bool playing: App.player.status === Playback.Playing
    readonly property bool loading: App.player.status === Playback.Loading
    // The song shown: it changes with a shared-axis move (SongSwap).
    readonly property var song: swap.song
    // The height below the cover.
    readonly property real restHeight: 32 + title.height + 4 + artistLine.implicitHeight + 24
                                       + seek.implicitHeight + elapsed.implicitHeight + 20
                                       + transport.implicitHeight + 20 + tray.height

    signal artistRequested(string key)
    signal songRequested
    signal loginRequested

    function openArtists(): void {
        if (song.artists.length === 1 && song.artists[0].key !== "")
            artistRequested(song.artists[0].key);
        else if (song.artists.length > 1)
            artistMenu.popup(artistLine, 0, artistLine.height + 4);
    }

    implicitHeight: coverSize + restHeight

    SongSwap {
        id: swap
        animate: root.visible
    }

    // Playing: full size, corners 32; paused: 92% and rounder (as the play button turns
    // round), on a spring.
    Item {
        id: coverBox
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.coverSize
        height: root.coverSize

        CoverArt {
            id: cover
            anchors.fill: parent
            opacity: (root.flying ? 0 : 1) * swap.fade
            coverKey: root.song.coverKey
            glyph: Symbols.music_note
            radius: rest.value * 24 + 32
            scale: Theme.motion.reduced ? 1 : 1 - rest.value * 0.08
            transform: Translate {
                x: swap.offset * 48
            }

            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                width: sourceText.implicitWidth + 20
                height: sourceText.implicitHeight + 12
                radius: height / 2
                color: Qt.alpha("black", 0.65)
                Text {
                    id: sourceText
                    anchors.centerIn: parent
                    text: App.player.provider === Playback.Bilibili ? "Bilibili" : qsTr("网易云")
                    color: "white"
                    font: Theme.type.labelLarge.font
                }
            }

            Spring {
                id: rest
                target: root.playing || root.loading ? 0 : 1
                spec: Theme.motion.defaultSpatial
                Component.onCompleted: snapTo(target)
            }
        }
    }

    Text {
        id: title
        anchors.top: coverBox.bottom
        anchors.topMargin: 32
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(implicitWidth, parent.width)
        horizontalAlignment: Text.AlignHCenter
        opacity: swap.fade
        transform: Translate {
            x: swap.offset * 24
        }
        text: root.song.title
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        color: root.tones.ink
        font: Fonts.font(600, 28)
        Accessible.role: Accessible.Heading

        // The song's page (as the default style's title).
        HoverHandler {
            enabled: !root.song.voice
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: !root.song.voice
            onTapped: root.songRequested()
        }
    }
    Text {
        id: artistLine
        anchors.top: title.bottom
        anchors.topMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(implicitWidth, parent.width)
        text: root.song.artistText
        elide: Text.ElideRight
        color: root.tones.inkVariant
        font: Theme.type.titleMedium.font
        opacity: swap.fade
        transform: Translate {
            x: swap.offset * 24
        }

        HoverHandler {
            enabled: root.song.artists.length > 0
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: root.openArtists()
        }
    }

    WavySeek {
        id: seek
        anchors.top: artistLine.bottom
        anchors.topMargin: 24
        width: parent.width
        tones: root.tones
        positionMs: root.positionMs
        playing: root.playing
    }
    Text {
        id: elapsed
        anchors.top: seek.bottom
        anchors.left: parent.left
        text: App.format.duration(seek.shownMs)
        color: root.tones.inkVariant
        font: Fonts.tabular(Theme.type.labelLarge.font)
    }
    Text {
        anchors.top: seek.bottom
        anchors.right: parent.right
        text: App.player.durationMs <= 0 ? "--:--"
              : App.settings.playerTimeRight === 1 ? App.format.duration(App.player.durationMs)
              : "-" + App.format.duration(App.player.durationMs - seek.shownMs)
        color: root.tones.inkVariant
        font: Fonts.tabular(Theme.type.labelLarge.font)
    }

    // Previous, play or pause, next: an MD3E standard button group of Large buttons (a
    // pressed one widens and squeezes its neighbours). The play button is square while
    // playing and round while paused.
    StandardButtonGroup {
        id: transport
        anchors.top: elapsed.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            size: Md.Large
            widthMode: Md.Wide
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
            anchors.verticalCenter: parent.verticalCenter
            variant: Md.Filled
            size: Md.Large
            widthMode: Md.Wide
            shape: root.playing || root.loading ? Md.Square : Md.Round
            glyph: root.loading ? "" : root.playing ? Symbols.pause : Symbols.play_arrow
            iconFill: 1
            text: root.loading ? qsTr("正在加载") : root.playing ? qsTr("暂停") : qsTr("播放")
            onClicked: App.player.playPause()

            LoadingIndicator {
                anchors.centerIn: parent
                implicitWidth: 40
                implicitHeight: 40
                visible: root.loading
                color: ColorScope.scheme.onPrimary
            }
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            size: Md.Large
            widthMode: Md.Wide
            containerTint: root.tones.raised
            tint: root.tones.ink
            enabled: App.player.canNext
            glyph: Symbols.skip_next
            iconFill: 1
            text: qsTr("下一首")
            onClicked: App.player.next()
        }
    }

    // The tray: shuffle, repeat, like and the volume as connected buttons on a pill a
    // tone away from the page.
    Rectangle {
        id: tray
        anchors.top: transport.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        width: trayGroup.implicitWidth + 12
        height: trayGroup.implicitHeight + 12
        radius: height / 2
        color: root.tones.tray

        component TrayButton: IconButton {
            anchors.verticalCenter: parent.verticalCenter
            size: Md.Medium
            widthMode: Md.Wide
            containerTint: root.tones.trayButton
            tint: root.tones.ink
        }

        ConnectedButtonGroup {
            id: trayGroup
            anchors.centerIn: parent

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
                readonly property bool liked: App.library.likedRevision >= 0 && App.library.isLiked(App.player.current.key)
                enabled: !App.player.current.voice
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
                        App.library.setLiked(App.player.current.key, !liked);
                }
            }
            MiniVolume {
                anchors.verticalCenter: parent.verticalCenter
                size: Md.Medium
                widthMode: Md.Wide
                containerTint: root.tones.trayButton
                tint: root.tones.ink
            }
        }
    }

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
