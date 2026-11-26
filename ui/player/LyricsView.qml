pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The lyrics column (docs/ui/pages/player.md §5). Every line is a LyricLine from a
// Repeater, placed by the summed heights of the lines before it (`tops`) so that the
// current line (the anchor) sits at 30% of the height (or centered); a new current
// line sets off the waterfall. Scrolling or dragging browses (§5.4): the track moves
// by hand, nothing follows, a click jumps to a line; three seconds later it returns.
FocusScope {
    id: root

    required property PlayerPalette colors
    // The smooth playback position (PlaybackClock).
    property real positionMs

    // The copy dialog, from the line menu.
    signal copyRequested

    // Reads Fonts.sans and Fonts.fontFiles so the fonts follow a font change, or a
    // font file loaded later.
    function fontAt(pixelSize: real): font {
        Fonts.sans;
        Fonts.fontFiles;
        return Fonts.fontOf(family, weight, pixelSize);
    }
    function openLineMenu(index: int, text: string, item: Item, at: point): void {
        lineMenu.text = text;
        lineMenu.popup(item, at.x, at.y);
    }

    readonly property int status: App.lyrics.status
    readonly property bool ready: status === Lyrics.Ready
    readonly property bool plain: App.lyrics.timing === Lyrics.PlainText
    // Plain text: all lines lit, free scrolling, no jumping.
    readonly property bool allLit: plain
    readonly property bool seekable: ready && !plain
    // Line and word times.
    readonly property real timeMs: positionMs - App.lyrics.shiftMs
    readonly property var active: App.lyrics.activeLines
    readonly property int current: App.lyrics.current

    // Settings (§9.2).
    readonly property real fontSize: Math.max(16, Math.min(96, App.settings.lyricSize))
    readonly property int weight: Math.max(100, Math.min(900, App.settings.lyricWeight))
    // 0 left, 1 centered, 2 right.
    readonly property int align: Math.max(0, Math.min(2, App.settings.lyricAlign))
    readonly property int textAlign: [Text.AlignLeft, Text.AlignHCenter, Text.AlignRight][align]
    // How far the hover layer reaches past the text on each side.
    readonly property real hoverInset: 20
    readonly property bool wordFill: App.settings.lyricWordFill
                                     && App.lyrics.timing === Lyrics.Words
    readonly property bool rubyOn: App.settings.lyricRuby && App.settings.showRomanization
    readonly property bool blurOn: App.settings.lyricBlur
    readonly property bool glow: App.settings.lyricGlow && !Theme.motion.reduced
    readonly property bool showTranslation: App.settings.showTranslation
    readonly property bool showRomanization: App.settings.showRomanization
    readonly property bool shaders: GraphicsInfo.api !== GraphicsInfo.Software
                                    && GraphicsInfo.api !== GraphicsInfo.Unknown
    // The lyrics' own family (a setting): "" follows the UI font.
    readonly property string family: App.settings.lyricFontSource === 2
                                     ? App.settings.lyricFontFamily : ""
    readonly property font lineFont: fontAt(fontSize)
    readonly property font smallFont: fontAt(fontSize * 0.6)
    readonly property font rubyFont: fontAt(fontSize * 0.42)
    readonly property real rubyRoom: rubyOn && App.lyrics.hasRuby ? fontSize * 0.42 * 1.35 : 0
    readonly property real spaceWidth: space.advanceWidth
    readonly property real lineGap: fontSize * Math.max(0, App.settings.lyricSpacing) / 10

    // --- Layout ------------------------------------------------------------------------

    // Each line's top within the track, and the track's height.
    property var tops: []
    property real totalHeight: 0
    // Snap instead of gliding (a new song, a resize).
    property bool instant: true

    readonly property int count: lines.count
    function lineAt(index: int): LyricLine {
        return lines.itemAt(index) as LyricLine;
    }
    function layout(): void {
        const next = [];
        let y = 0;
        for (let i = 0; i < lines.count; ++i) {
            next.push(y);
            const item = lineAt(i);
            y += (item ? item.implicitHeight : 0) + lineGap;
        }
        tops = next;
        totalHeight = y;
    }
    function scheduleLayout(): void {
        Qt.callLater(layout);
    }
    function snapNextLayout(): void {
        instant = true;
        scheduleLayout();
        Qt.callLater(() => root.instant = false);
    }
    onWidthChanged: snapNextLayout()
    onFontSizeChanged: snapNextLayout()
    onLineGapChanged: snapNextLayout()
    Connections {
        target: App.lyrics.lines
        function onModelReset() {
            root.stopBrowsing(false);
            root.snapNextLayout();
        }
    }

    // The upcoming line during a long gap before it (§5.5): its dots take the anchor.
    readonly property int interludeIndex: {
        if (!ready || plain)
            return -1;
        const next = current + 1;
        const item = next < count ? lineAt(next) : null;
        if (!item || item.gapMs < 4000)
            return -1;
        return timeMs >= item.startMs - item.gapMs && timeMs < item.startMs ? next : -1;
    }
    // The line at the anchor position.
    readonly property int liveAnchor: plain ? 0
                                            : interludeIndex >= 0 ? interludeIndex
                                                                  : active.length > 0 ? active[0]
                                                                                      : Math.max(0, current)
    property int frozenAnchor: 0
    // A jump over many lines: the lines slide in as a block (LyricLine) and the track
    // fades in, covering the lines that leave at once.
    property int lastAnchor: 0
    onAnchorIndexChanged: {
        if (!instant && Math.abs(anchorIndex - lastAnchor) > 2 && !Theme.motion.reduced)
            jumpIn.restart();
        lastAnchor = anchorIndex;
    }
    readonly property int anchorIndex: plain ? 0 : browsing ? frozenAnchor : liveAnchor
    readonly property real anchorY: {
        if (plain)
            return notice.height + 24;
        if (App.settings.lyricAnchor === 1) {
            const item = lineAt(anchorIndex);
            return (height - (item ? item.implicitHeight : 0)) / 2;
        }
        return height * 0.3;
    }

    // --- Browsing (§5.4) -----------------------------------------------------------------

    // Browsing by hand; plain text is always browsed.
    property bool handBrowsing: false
    readonly property bool browsing: plain || handBrowsing
    // The track's offset while browsing; it springs back to 0 afterwards.
    property real browseOffset: 0
    readonly property real scrollY: scroll.value
    // 0.4 for the other lines while following, 0.6 while browsing.
    readonly property real dimLevel: 0.4 + 0.2 * browseLevel.value
    // A line chosen with ↑ ↓ (keyboard browsing); -1 none.
    property int keyIndex: -1
    readonly property bool keyboardBrowsing: activeFocus && InputMode.keyboard && keyIndex >= 0

    function startBrowsing(): void {
        if (!browsing) {
            frozenAnchor = liveAnchor;
            handBrowsing = true;
        }
        if (!plain)
            browseTimer.restart();
    }
    function stopBrowsing(animate: bool): void {
        browseTimer.stop();
        handBrowsing = false;
        keyIndex = -1;
        browseOffset = 0;
        if (animate)
            scroll.target = 0;
        else
            scroll.snapTo(0);
    }
    function scrollTo(offset: real): void {
        const first = tops[0] ?? 0;
        const last = tops[Math.max(0, count - 1)] ?? 0;
        const anchorTop = tops[anchorIndex] ?? 0;
        browseOffset = Math.max(-(last - anchorTop), Math.min(anchorTop - first, offset));
        scroll.snapTo(browseOffset);
    }
    function scrollBy(delta: real): void {
        startBrowsing();
        scrollTo(browseOffset + delta);
    }
    function seekToLine(index: int): void {
        const item = lineAt(index);
        if (!item || !seekable)
            return;
        App.player.seek(Math.max(0, item.startMs + App.lyrics.shiftMs));
        stopBrowsing(true);
    }
    function moveKey(step: int): void {
        startBrowsing();
        const from = keyIndex >= 0 ? keyIndex : anchorIndex;
        keyIndex = Math.max(0, Math.min(count - 1, from + step));
        scrollTo(-((tops[keyIndex] ?? 0) - (tops[anchorIndex] ?? 0)));
    }

    Timer {
        id: browseTimer
        interval: 3000
        onTriggered: root.stopBrowsing(true)
    }
    Spring {
        id: scroll
        spec: Theme.motion.defaultSpatial
    }
    Spring {
        id: browseLevel
        target: root.browsing && !root.plain ? 1 : 0
        spec: Theme.motion.defaultEffects
    }

    activeFocusOnTab: ready && count > 0
    Accessible.role: Accessible.List
    Accessible.name: qsTr("歌词")
    Keys.onPressed: event => {
        if (!root.ready)
            return;
        switch (event.key) {
        case Qt.Key_Up:
            root.moveKey(-1);
            break;
        case Qt.Key_Down:
            root.moveKey(1);
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (root.keyIndex < 0 || !root.seekable)
                return;
            root.seekToLine(root.keyIndex);
            break;
        case Qt.Key_Escape:
            if (!root.handBrowsing)
                return;
            root.stopBrowsing(true);
            break;
        default:
            return;
        }
        event.accepted = true;
    }
    onActiveFocusChanged: if (!activeFocus)
                              keyIndex = -1

    Menu {
        id: lineMenu
        property string text
        MenuItem {
            text: qsTr("复制这一行")
            glyph: Symbols.content_copy
            onTriggered: App.copyText(lineMenu.text)
        }
        MenuItem {
            text: qsTr("复制歌曲信息")
            glyph: Symbols.lyrics
            onTriggered: root.copyRequested()
        }
    }

    TextMetrics {
        id: space
        font: root.lineFont
        text: " "
    }

    // --- Lines -----------------------------------------------------------------------

    Item {
        id: viewport
        // Wider than the column by the hover layer's reach, which the clip would cut.
        anchors.fill: parent
        anchors.leftMargin: -root.hoverInset
        anchors.rightMargin: -root.hoverInset
        clip: true
        visible: root.ready
        // Lines fade out at the edges rather than being cut.
        layer.enabled: root.shaders
        layer.effect: ShaderEffect {
            property var source
            readonly property real fadeTop: Math.min(0.4, root.fontSize * 1.5 / Math.max(1, viewport.height))
            readonly property real fadeBottom: Math.min(0.4, root.fontSize * 2.5 / Math.max(1, viewport.height))
            fragmentShader: Qt.resolvedUrl("shaders/edgefade.frag.qsb")
        }

        // The wheel browses (a MouseArea over the lines that takes nothing else: the
        // lines keep their hover and clicks). Touchpads scroll by pixels, wheels by
        // notches of about two lines (times the user's Wheel.speed).
        MouseArea {
            anchors.fill: parent
            z: 1
            acceptedButtons: Qt.NoButton
            onWheel: wheel => {
                const pixels = wheel.pixelDelta.y !== 0 && wheel.phase !== Qt.NoScrollPhase;
                root.scrollBy(pixels ? wheel.pixelDelta.y : wheel.angleDelta.y / 120 * root.fontSize * 2.5 * Wheel.speed);
            }
        }
        DragHandler {
            id: drag
            target: null
            xAxis.enabled: false
            property real startOffset
            onActiveChanged: if (active) {
                                 root.startBrowsing();
                                 startOffset = root.browseOffset;
                             } else if (!root.plain) {
                                 browseTimer.restart();
                             }
            onActiveTranslationChanged: if (active)
                                            root.scrollTo(startOffset + activeTranslation.y)
        }

        Item {
            id: track
            x: root.hoverInset
            width: root.width
            y: root.scrollY

            NumberAnimation {
                id: jumpIn
                target: track
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.defaultEffects.duration
                easing.type: Easing.OutCubic
            }

            Repeater {
                id: lines
                model: App.lyrics.lines
                onItemAdded: root.scheduleLayout()
                onItemRemoved: root.scheduleLayout()

                LyricLine {
                    view: root
                }
            }
        }
    }

    // Plain text has no timing: a note above it.
    Text {
        id: notice
        width: parent.width
        visible: root.ready && root.plain
        height: visible ? implicitHeight : 0
        text: qsTr("歌词没有时间信息")
        horizontalAlignment: root.textAlign
        color: root.colors.contentVariant
        font: Fonts.font(500, 14)
    }

    // --- Without lyrics (§5.7) -------------------------------------------------------

    Column {
        y: parent.height * 0.3
        width: parent.width
        spacing: root.fontSize * 0.6
        visible: root.status === Lyrics.Loading

        Repeater {
            model: [0.7, 0.5, 0.62]
            Rectangle {
                required property real modelData
                x: (parent.width - width) * [0, 0.5, 1][root.align]
                width: parent.width * modelData
                height: root.fontSize * 0.8
                radius: height / 2
                color: Qt.alpha(root.colors.content, 0.1)
            }
        }
    }
    Column {
        y: parent.height * 0.3
        width: parent.width
        spacing: 16
        visible: root.status === Lyrics.NoLyrics || root.status === Lyrics.Instrumental
                 || root.status === Lyrics.Failed

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            horizontalAlignment: root.textAlign
            color: root.colors.contentVariant
            font: root.lineFont
            text: root.status === Lyrics.Instrumental ? qsTr("纯音乐，请欣赏")
                : root.status === Lyrics.Failed ? qsTr("歌词加载失败") : qsTr("暂无歌词")
        }
        Button {
            x: (parent.width - width) * [0, 0.5, 1][root.align]
            visible: root.status === Lyrics.Failed
            variant: Md.Tonal
            glyph: Symbols.refresh
            text: qsTr("重试")
            onClicked: App.lyrics.retry()
        }
    }
}
