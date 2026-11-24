pragma ComponentBehavior: Bound
import QtQuick
import M3Shapes
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Personal FM beside "today" on the home page (docs/ui/redesign-md3e.md §6.2): a tonal
// container with the play button as a scalloped shape in the tertiary color, which
// turns while FM plays. The tertiary role is on the button only: as the container it is
// far brighter than "today" in the dark scheme and takes over the page. Shows what FM is playing
// once it plays, otherwise what it is. `strip`: the 72 px row of medium and narrow
// windows, with ordinary buttons.
Rectangle {
    id: card

    property bool strip: false

    property AppShell shell
    readonly property bool fm: App.player.mode === Playback.PersonalFm
    readonly property bool loading: fm && (App.player.status === Playback.Loading
                                           || App.player.fmWaiting)
    readonly property bool playing: fm && (App.player.status === Playback.Playing || loading)
    readonly property bool hasSong: fm && App.player.current.key !== ""
    readonly property color content: Theme.color.onSecondaryContainer

    radius: strip ? Theme.shape.large : 40
    color: Theme.color.secondaryContainer
    clip: true

    // Play or pause FM; starts it when something else is playing.
    function toggle() {
        if (fm)
            App.player.playPause();
        else if (shell)
            shell.startFm(); // asks first while listening together
        else
            App.player.startFm();
    }

    // Cover of the FM song, or the radio glyph on a faint disc.
    Item {
        id: art
        x: card.strip ? 16 : 24
        y: card.strip ? (card.height - height) / 2 : 24
        width: card.strip ? 40 : 44
        height: width

        Rectangle {
            anchors.fill: parent
            visible: !card.hasSong
            radius: width / 2
            color: Qt.alpha(card.content, 0.16)
            Icon {
                anchors.centerIn: parent
                glyph: Symbols.radio
                size: 24
                color: card.content
            }
        }
        CoverArt {
            anchors.fill: parent
            visible: card.hasSong
            coverKey: App.player.current.coverKey
            radius: Theme.shape.medium
        }
    }

    Column {
        id: texts
        x: card.strip ? art.x + art.width + 16 : 24
        y: card.strip ? (card.height - height) / 2 : card.height - height - 24
        width: (card.strip ? buttons.x : shapeButton.x) - 16 - x
        spacing: 2
        Text {
            width: parent.width
            text: card.hasSong ? App.player.current.title : qsTr("私人 FM")
            elide: Text.ElideRight
            color: card.content
            font: card.strip ? Theme.type.titleMedium.font
                : card.hasSong ? Theme.type.titleLargeEmphasized.font : Fonts.font(800, 34)
        }
        Text {
            width: parent.width
            text: card.hasSong ? qsTr("私人 FM · %1").arg(App.player.current.artistText)
                               : qsTr("按你的口味一直放下去")
            elide: Text.ElideRight
            color: card.content
            opacity: 0.85
            font: Theme.type.bodyMedium.font
        }
    }

    // The card's own play button, and "next" above the words once FM plays.
    ShapeButton {
        id: shapeButton
        visible: !card.strip
        x: card.width - width - 24
        anchors.verticalCenter: parent.verticalCenter
        size: Math.min(112, card.height - 48)
        tone: Md.Tertiary
        shape: MaterialShape.Cookie12Sided
        spinning: card.playing && !card.loading
        glyph: card.loading ? "" : card.playing ? Symbols.pause : Symbols.play_arrow
        text: card.loading ? qsTr("正在加载") : card.playing ? qsTr("暂停私人 FM")
                                                            : qsTr("播放私人 FM")
        onClicked: card.toggle()

        LoadingIndicator {
            anchors.centerIn: parent
            implicitWidth: 36
            implicitHeight: 36
            visible: card.loading
            color: Theme.color.onTertiary
        }
    }
    IconButton {
        visible: !card.strip && card.fm
        x: art.x + art.width + 8
        anchors.verticalCenter: art.verticalCenter
        enabled: !App.player.fmWaiting
        tint: card.content
        glyph: Symbols.skip_next
        iconFill: 1
        text: qsTr("下一首")
        onClicked: App.player.next()
    }

    Row {
        id: buttons
        visible: card.strip
        x: card.width - width - 16
        y: (card.height - height) / 2
        spacing: 8

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            variant: Md.Filled
            widthMode: Md.Wide
            shape: card.playing ? Md.Square : Md.Round
            glyph: card.loading ? "" : card.playing ? Symbols.pause : Symbols.play_arrow
            iconFill: 1
            text: card.loading ? qsTr("正在加载") : card.playing ? qsTr("暂停私人 FM")
                                                                : qsTr("播放私人 FM")
            onClicked: card.toggle()

            LoadingIndicator {
                anchors.centerIn: parent
                implicitWidth: 28
                implicitHeight: 28
                visible: card.loading
                color: Theme.color.onPrimary
            }
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: card.fm
            enabled: !App.player.fmWaiting
            tint: card.content
            glyph: Symbols.skip_next
            iconFill: 1
            text: qsTr("下一首")
            onClicked: App.player.next()
        }
    }
}
