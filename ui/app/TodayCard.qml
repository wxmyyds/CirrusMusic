pragma ComponentBehavior: Bound
import QtQuick
import M3Shapes
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// "Today" at the top of the home page, the page's hero (docs/ui/redesign-md3e.md §6.2):
// the date, "今日推荐" in heavy display type, the largest play button of the app, and
// the first songs' covers cut to shapes with the day of the month as a sticker. Guests
// get the sign-in invitation in the same place. The sizes step down with the card's
// width (`tier`).
Rectangle {
    id: card

    required property AppShell shell
    required property DailySongsPageModel daily

    readonly property date today: new Date()
    readonly property bool signedIn: App.session.signedIn
    readonly property int loadState: daily.state
    readonly property bool ready: loadState === LoadState.Ready || loadState === LoadState.Partial
    readonly property int songCount: daily.songs.count
    readonly property bool hasSongs: signedIn && ready && songCount > 0
    // Today's songs are what is playing (not personal FM picking one of them).
    readonly property bool active: daily.songs.containsCurrent && App.player.mode === Playback.Queue
    readonly property bool playing: active && (App.player.status === Playback.Playing
                                               || App.player.status === Playback.Loading)

    // 2: XLarge buttons and 88 px type; 1: Large and 64; 0: Medium and 44.
    readonly property int tier: width >= 640 ? 2 : width >= 420 ? 1 : 0
    readonly property real pad: tier === 2 ? 40 : tier === 1 ? 32 : 24
    readonly property int buttonSize: [Md.Medium, Md.Large, Md.XLarge][tier]
    // "All N songs" in words where the covers keep room beside it, else as an icon.
    readonly property bool allInWords: tier === 2 && width - pad - 456 - 32 >= 220
    // Room for the covers beside the buttons.
    readonly property real artRoom: width - pad - actions.width - 32
    readonly property bool showArt: tier === 2 && hasSongs && artRoom >= 200
    readonly property color content: Theme.color.onPrimaryContainer

    implicitHeight: lines.height + actions.height + [16, 20, 24][tier] + (tier === 2 ? 32 : 24) * 2
    radius: tier === 0 ? Theme.shape.extraLarge : 48
    color: Theme.color.primaryContainer
    clip: true

    // The covers turn a little while hovered.
    Spring {
        id: lean
        target: hover.hovered && card.hasSongs ? 1 : 0
        spec: Theme.motion.defaultSpatial
    }

    HoverHandler {
        id: hover
        cursorShape: card.hasSongs ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
    TapHandler {
        enabled: card.hasSongs
        onTapped: card.shell.openDaily("")
    }
    StateLayer {
        anchors.fill: parent
        radius: card.radius
        color: card.content
        hovered: hover.hovered && card.hasSongs
    }

    Column {
        id: lines
        x: card.pad
        y: card.tier === 2 ? 32 : 24
        width: (card.showArt ? art.x - 16 : card.width - card.pad) - x
        spacing: card.tier === 2 ? 2 : 4

        Text {
            width: parent.width
            text: qsTr("%1月%2日 · %3").arg(card.today.getMonth() + 1).arg(card.today.getDate())
                                       .arg(Qt.locale().toString(card.today, "dddd"))
            elide: Text.ElideRight
            color: card.content
            font: Theme.type.titleMedium.font
            Accessible.name: Qt.locale().toString(card.today, Locale.LongFormat)
        }
        Text {
            id: title
            readonly property real size: [44, 64, 88][card.tier]
            width: parent.width
            text: qsTr("今日推荐")
            elide: Text.ElideRight
            color: card.content
            font: Fonts.font(800, size)
            verticalAlignment: Text.AlignVCenter
            Accessible.role: Accessible.Heading
        }

        // Loaded: how many, and the first song.
        Text {
            width: parent.width
            visible: card.hasSongs
            text: qsTr("%n 首 · 第一首《%1》", "", card.songCount).arg(card.daily.lead.title)
            elide: Text.ElideRight
            color: card.content
            opacity: 0.85
            font: Theme.type.bodyLarge.font
        }
        // Loading: a placeholder for that line.
        Skeleton {
            visible: card.signedIn && !card.ready && card.loadState === LoadState.Loading
            width: Math.min(260, lines.width)
            height: 18
            color: Qt.alpha(card.content, 0.12)
        }
        // Anything else worth a sentence: empty, failed, signed out.
        Text {
            width: parent.width
            visible: text !== ""
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: card.content
            font: Theme.type.bodyLarge.font
            text: {
                if (App.session.status === Account.Expired)
                    return qsTr("登录已过期，重新扫码登录后恢复今日推荐和私人 FM。");
                if (!card.signedIn)
                    return qsTr("登录后，这里每天都有一组为你挑的歌。私人 FM 也需要登录。");
                if (card.loadState === LoadState.Empty)
                    return qsTr("今天的推荐还没生成，稍后刷新试试。");
                if (card.loadState === LoadState.Offline)
                    return qsTr("网络不可用，今日推荐没有加载出来。");
                if (card.loadState === LoadState.Error)
                    return qsTr("今日推荐没有加载出来。");
                return "";
            }
        }
    }

    Row {
        id: actions
        x: card.pad
        anchors.bottom: parent.bottom
        anchors.bottomMargin: card.tier === 2 ? 32 : 24
        spacing: 8

        Button {
            visible: card.hasSongs
            variant: Md.Filled
            size: card.buttonSize
            glyph: card.playing ? Symbols.pause : Symbols.play_arrow
            iconFill: 1
            text: card.playing ? qsTr("暂停") : qsTr("播放")
            font: card.tier === 2 ? Fonts.font(700, 32) : labelStyle.font
            onClicked: {
                if (card.active)
                    App.player.playPause();
                else
                    App.player.playSongs(card.daily.songs.keys(), 0, Playback.FromDaily);
            }
        }
        // The whole list; the card itself opens it too.
        Button {
            visible: card.hasSongs && card.allInWords
            variant: Md.Tonal
            size: card.buttonSize
            shape: Md.Square
            leftPadding: 32
            rightPadding: 32
            width: implicitContentWidth + 64
            text: qsTr("全部 %n 首", "", card.songCount)
            font: Fonts.font(700, 22)
            onClicked: card.shell.openDaily("")
        }
        IconButton {
            visible: card.hasSongs && !card.allInWords
            variant: Md.Tonal
            size: card.buttonSize
            shape: Md.Square
            widthMode: Md.Narrow
            glyph: Symbols.queue_music
            text: qsTr("全部 %n 首", "", card.songCount)
            onClicked: card.shell.openDaily("")
        }
        Button {
            visible: card.signedIn && (card.loadState === LoadState.Error
                                       || card.loadState === LoadState.Offline)
            variant: Md.Tonal
            size: Md.Medium
            text: qsTr("重试")
            onClicked: card.daily.retry()
        }
        Button {
            visible: !card.signedIn
            variant: Md.Filled
            size: card.tier === 0 ? Md.Medium : Md.Large
            glyph: Symbols.qr_code_2
            text: qsTr("扫码登录")
            onClicked: card.shell.openLogin()
        }
    }

    // The first three covers cut to shapes, and the day as a sticker. Each sits on a
    // slightly larger copy of its shape in the card's color, which cuts it out of the
    // ones behind. Decorative: the whole covers are in the list one click away.
    Item {
        id: art
        readonly property real unit: Math.min(1, card.artRoom / 300, (card.height - 32) / 340)
        visible: card.showArt
        width: 300 * unit
        height: 340 * unit
        x: card.width - width - 24
        y: (card.height - height) / 2

        component Cut: Item {
            id: cut
            property int shape
            property string coverKey
            property real turn: 0
            // Shown inside: a cover by default.
            default property alias inside: holder.data
            rotation: turn * lean.value

            MorphShape {
                anchors.fill: parent
                anchors.margins: -6 * art.unit
                shape: cut.shape
                color: card.color
                fallbackRadius: Theme.shape.extraLarge
            }
            Item {
                id: holder
                anchors.fill: parent
                CoverArt {
                    anchors.fill: parent
                    visible: cut.coverKey !== ""
                    coverKey: cut.coverKey
                    shape: cut.shape
                    radius: Theme.shape.extraLarge
                }
            }
        }

        Cut {
            x: 20 * art.unit
            y: 28 * art.unit
            width: 240 * art.unit
            height: width
            shape: MaterialShape.Cookie9Sided
            coverKey: card.daily.coverKeys.length > 0 ? card.daily.coverKeys[0] : ""
            turn: 12
        }
        Cut {
            x: 184 * art.unit
            y: 180 * art.unit
            width: 112 * art.unit
            height: 150 * art.unit
            shape: MaterialShape.Arch
            visible: card.daily.coverKeys.length > 1
            coverKey: card.daily.coverKeys.length > 1 ? card.daily.coverKeys[1] : ""
        }
        Cut {
            x: 36 * art.unit
            y: 262 * art.unit
            width: 68 * art.unit
            height: width
            shape: MaterialShape.Circle
            visible: card.daily.coverKeys.length > 2
            coverKey: card.daily.coverKeys.length > 2 ? card.daily.coverKeys[2] : ""
        }
        Cut {
            id: sticker
            x: 188 * art.unit
            y: 0
            width: 112 * art.unit
            height: width
            shape: MaterialShape.Cookie7Sided
            turn: -18

            MorphShape {
                anchors.fill: parent
                shape: MaterialShape.Cookie7Sided
                color: Theme.color.tertiary
                fallbackRadius: width / 2
            }
            Text {
                anchors.centerIn: parent
                // Counter the sticker's turn: the day stays upright.
                rotation: -sticker.rotation
                text: Qt.formatDate(card.today, "d")
                color: Theme.color.onTertiary
                font: Fonts.font(800, Math.round(46 * art.unit))
                Accessible.ignored: true
            }
        }
    }
}
