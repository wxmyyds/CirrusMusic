pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Transport row (docs/ui/pages/player.md §1.1): play mode, previous, play/pause,
// next, like, spread over the width with play/pause in the middle. The play mode is
// one button cycling as NetEase does: list repeat → repeat one → shuffle → in order
// (→ 心动模式 while the liked songs play).
Item {
    id: root

    required property PlayerPalette colors
    readonly property Item playButton: play
    // Previous, play and next, for the flight from the mini player's buttons.
    readonly property rect transportRect: Qt.rect(previous.x, play.y,
                                                  next.x + next.width - previous.x, play.height)
    // Smaller, for the bottom bar.
    property bool compact: false

    // Not signed in: liking needs the sign-in first.
    signal loginRequested

    // App.player.playMode: 0 list repeat, 1 repeat one, 2 shuffle, 3 in order (stops at the
    // end), 4 心动模式 (docs/tasks/heartbeat-mode.md §7.2).
    readonly property var modeNames: [qsTr("列表循环"), qsTr("单曲循环"), qsTr("随机播放"),
        qsTr("顺序播放"), qsTr("心动模式")]
    readonly property var modeGlyphs: [Symbols.repeat, Symbols.repeat_one, Symbols.shuffle,
        Symbols.playlist_play, Symbols.ecg_heart]

    implicitHeight: play.implicitHeight

    PlayerButton {
        id: modeButton
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        color: root.colors.content
        iconSize: root.compact ? 20 : 22
        glyph: root.modeGlyphs[App.player.playMode]
        text: root.modeNames[App.player.playMode]
        onClicked: App.player.cyclePlayMode()

        // A heartbeat mix being fetched (usually under a second).
        CircularProgress {
            anchors.centerIn: parent
            width: modeButton.iconSize + 14
            height: width
            thickness: 2
            indeterminate: true
            visible: App.player.heartbeatLoading
        }
    }

    PlayerButton {
        id: previous
        anchors.right: play.left
        anchors.rightMargin: Math.min(root.compact ? 12 : 24, root.width / 16)
        anchors.verticalCenter: parent.verticalCenter
        color: root.colors.content
        iconSize: root.compact ? 28 : 36
        enabled: App.player.canPrevious
        glyph: Symbols.skip_previous
        text: qsTr("上一首")
        onClicked: App.player.previous()
    }

    PlayerButton {
        id: play
        readonly property bool loading: App.player.status === Playback.Loading
        readonly property bool playing: App.player.status === Playback.Playing || loading
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        color: root.colors.content
        iconSize: root.compact ? 40 : 52
        glyph: playing ? Symbols.pause : Symbols.play_arrow
        text: loading ? qsTr("正在加载") : playing ? qsTr("暂停") : qsTr("播放")
        onClicked: App.player.playPause()
    }

    PlayerButton {
        id: next
        anchors.left: play.right
        anchors.leftMargin: Math.min(root.compact ? 12 : 24, root.width / 16)
        anchors.verticalCenter: parent.verticalCenter
        color: root.colors.content
        iconSize: root.compact ? 28 : 36
        enabled: App.player.canNext
        glyph: Symbols.skip_next
        text: qsTr("下一首")
        onClicked: App.player.next()
    }

    PlayerButton {
        readonly property string key: App.player.current.key
        readonly property bool liked: App.library.likedRevision >= 0 && App.library.isLiked(key)
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        visible: key !== "" && !App.player.current.voice
        color: root.colors.content
        iconSize: root.compact ? 20 : 22
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
}
