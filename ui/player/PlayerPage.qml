pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation
import Cirrus.Window

// The player page (docs/ui/pages/player.md): over the whole window, title bar row
// included, in the colors of the playing song's cover. Three views (cover, lyrics,
// queue), the controls under the cover or at the bottom, and while playing they hide
// after a few seconds without input. Opening grows it out of the mini player with the
// cover flying across (docs/ui/motion-choreography.md §2).
//   AppWindow { overlay: PlayerPage { appWindow: window; miniBar: …; miniCover: … } }
FocusScope {
    id: page

    required property AppWindow appWindow
    // The mini player's card and cover: where opening starts and closing ends. Null:
    // the page slides up instead.
    property Item miniBar
    property Item miniCover
    // A mini cover cut to a shape (MaterialShape, -1 none) and turned, as the docked
    // side player's is; `miniCoverCorner` its corner radius when it isn't a CoverArt.
    property int miniCoverShape: -1
    property real miniCoverRotation: 0
    property real miniCoverCorner: -1
    // The mini player's title, buttons and seek bar: they fly to the page's title,
    // transport buttons and progress bar, cross-fading with them.
    property Item miniTitle
    property Item miniControls
    property Item miniSeek
    // The display settings panel's content (the app's PlayerPanelSettings); without
    // it there is no settings button.
    property Component settingsContent
    // The side panel open (§8, §9.1): 0 none, 1 the queue, 2 the display settings.
    property int sheet: 0
    readonly property bool settingsOpen: sheet === 2

    property bool opened: false
    readonly property bool reduced: Theme.motion.reduced
    // 0 closed to 1 open, overshooting a little (geometry); and the fade that replaces
    // it with reduced motion.
    readonly property real progress: grow.value
    // The shell is completely hidden behind the page (it may stop drawing).
    readonly property bool covering: reduced ? fade.value >= 0.999 : grow.value >= 0.6
    // The cover is flying between the mini player and the page.
    readonly property bool flying: !reduced && miniCover !== null && (grow.running
                                                                       || (grow.value > 0.001
                                                                           && grow.value < 0.999))

    // 0 cover, 1 lyrics (the queue is a side panel now).
    property int view: Math.min(1, App.settings.playerView)
    // The Material style (docs/ui/pages/player-expressive.md) instead of the default.
    readonly property bool expressive: App.settings.playerStyle === 1
    readonly property ExpressivePlayer expressiveItem: expressiveLoader.item as ExpressivePlayer
    readonly property bool wide: width >= 1000
    // Where the controls and the title go, as set; changes glide (see the Connections).
    // Not bindings: a binding would move them before the glide is switched on.
    property int controlsPlace: -1
    property int headerPlace: -1
    Component.onCompleted: {
        controlsPlace = App.settings.playerControls;
        headerPlace = App.settings.playerHeader;
    }
    readonly property bool controlsAtBottom: wide && controlsPlace === 1
    readonly property bool headerAtBottom: wide && headerPlace === 1
    // Views switch with motion; window resizes follow at once.
    property bool animateLayout: false

    // Controls, top buttons and the pointer are hidden (docs/ui/pages/player.md §2.2).
    property bool chromeHidden: false
    readonly property bool canHide: opened && !expressive && App.settings.playerAutoHide > 0
                                    && App.player.status === Playback.Playing
                                    && !controlsFocused
                                    && !overControls && !volume.barOpen && !header.menuOpen
                                    && !lyricsView.handBrowsing && sheet === 0 && !songInfo.opened
    // Someone is working the hiding controls with the keyboard: one of them has the
    // focus, reached by keyboard (the play button focused on opening does not count).
    readonly property bool controlsFocused: {
        if (!InputMode.keyboard)
            return false;
        const focused = appWindow.activeFocusItem;
        const control = focused as T.Control;
        if (!focused || (control && !control.visualFocus))
            return false;
        for (let item = focused; item; item = item.parent) {
            if (item === hideable)
                return true;
        }
        return false;
    }
    // Where the pointer last was: Qt Quick re-sends hover events at the same place
    // whenever the scene changes (lyrics scrolling), which must not count as movement.
    property point lastPointer: Qt.point(-1, -1)
    readonly property bool overControls: progressHover.hovered || controlsHover.hovered
                                         || rowHover.hovered || topHover.hovered

    property bool fullScreenByPlayer: false
    property int restoreVisibility: Window.Windowed

    signal commentsRequested
    // The playing song's own page (song.md §3).
    signal songRequested
    signal artistRequested(string key)
    signal albumRequested(string key)
    signal podcastRequested(string key) // a voice's podcast
    signal addToPlaylistRequested(string key, string title)
    signal loginRequested
    signal togetherRequested // listen together's dialog (pages/together.md §1)

    function open(): void {
        if (App.player.current.key === "" || opened)
            return;
        captureRects();
        opened = true;
        wake();
        (expressiveItem ? expressiveItem.playButton : controls.playButton).forceActiveFocus(Qt.OtherFocusReason);
    }
    // With the queue panel showing: the mini player's queue button.
    function openQueue(): void {
        open();
        if (opened)
            sheet = 1;
    }
    function close(): void {
        if (!opened)
            return;
        sheet = 0;
        if (fullScreenByPlayer)
            toggleFullScreen();
        captureRects();
        opened = false;
    }
    function setView(next: int): void {
        animateLayout = true;
        settle.restart();
        view = next;
        App.settings.playerView = next;
    }
    function toggleFullScreen(): void {
        if (appWindow.visibility === Window.FullScreen) {
            if (restoreVisibility === Window.Maximized)
                appWindow.showMaximized();
            else
                appWindow.showNormal();
            fullScreenByPlayer = false;
        } else {
            restoreVisibility = appWindow.visibility;
            appWindow.showFullScreen();
            fullScreenByPlayer = true;
        }
    }
    // A short message (Snackbar) above the bottom row, in the cover's colors: the
    // shell's notices while the page covers it.
    function notify(text: string): void {
        snackbar.show(text);
    }
    function notifyAction(text: string, action: string, handler: var): void {
        snackbar.showAction(text, action, handler);
    }
    // Shows the hidden controls and starts counting again.
    function wake(): void {
        chromeHidden = false;
        hideTimer.restart();
    }

    // --- Shared element geometry -----------------------------------------------------

    property rect miniBarRect: Qt.rect(0, height, width, 0)
    property rect miniCoverRect
    // The mini player's capsule and cover as they look, where the panel and the flying
    // cover start: the panel grows out of the capsule (its color and corners), the
    // cover morphs from its shape (a circle) to the page cover's.
    readonly property real miniBarRadius: (miniBar as Rectangle)?.radius ?? Theme.shape.large
    readonly property color miniBarColor: (miniBar as Rectangle)?.color ?? Theme.color.surfaceContainer
    readonly property real miniCoverRadius: miniCoverCorner >= 0 ? miniCoverCorner
                                          : (miniCover as CoverArt)?.radius ?? Theme.shape.small
    // The shape and turn the flight starts from, taken when it starts; and where it
    // lands on the Material style's shaped cover.
    property int flyShape: -1
    property real flyTurn: 0
    property int endShape: -1
    property real endTurn: 0
    readonly property real coverEndRadius: expressiveItem ? expressiveItem.coverRadius : cover.radius
    property rect coverRect
    property rect miniTitleRect
    property rect miniControlsRect
    property rect miniSeekRect
    function captureRects(): void {
        const rectOf = item => item ? item.mapToItem(page, 0, 0, item.width, item.height)
                                    : Qt.rect(0, 0, 0, 0);
        if (miniBar)
            miniBarRect = rectOf(miniBar);
        if (miniCover)
            miniCoverRect = rectOf(miniCover);
        flyShape = miniCoverShape;
        // The nearest upright turn of the shape is where it lands (its symmetry).
        const turn = miniCoverRotation % 360;
        flyTurn = turn > 180 ? turn - 360 : turn;
        miniTitleRect = rectOf(miniTitle);
        miniControlsRect = rectOf(miniControls);
        miniSeekRect = rectOf(miniSeek);
        const art = expressiveItem ? expressiveItem.coverItem : cover.art;
        coverRect = art.mapToItem(page, 0, 0, art.width, art.height);
        endShape = expressiveItem ? expressiveItem.coverShape : -1;
        const end = expressiveItem ? expressiveItem.coverRotation % 360 : 0;
        endTurn = end > 180 ? end - 360 : end;
    }

    // The other shared elements (docs/ui/pages/player.md §2.1). Each pair moves along
    // one path from the mini player's element (`from`, page coordinates) to the page's
    // (`to`); `source` is the rect being placed, the mini element's or the page's,
    // scaled with the path. Mode 0 keeps the left edge and the aspect (title), 1 the
    // center and the aspect (buttons), 2 fills the rect (bars).
    function placed(from: rect, to: rect, source: rect, mode: int, t: real): rect {
        const lerp = Theme.motion.lerp;
        const w = lerp(from.width, to.width, t), h = lerp(from.height, to.height, t);
        const x = lerp(from.x, to.x, t), y = lerp(from.y, to.y, t);
        if (mode === 2)
            return Qt.rect(x, y, w, h);
        const s = source.height > 0 ? h / source.height : 1;
        const sw = source.width * s, sh = source.height * s;
        return mode === 0 ? Qt.rect(x, y + (h - sh) / 2, sw, sh)
                          : Qt.rect(x + (w - sw) / 2, y + (h - sh) / 2, sw, sh);
    }
    // The transform that puts the part `local` of a page element, whose own place is
    // `at`, on `place`: {sx, sy, tx, ty} for a Scale (origin 0, 0) then a Translate.
    function moveOf(local: rect, at: point, place: rect): var {
        const sx = local.width > 0 ? place.width / local.width : 1;
        const sy = local.height > 0 ? place.height / local.height : 1;
        return {
            sx: sx,
            sy: sy,
            tx: place.x - at.x - local.x * sx,
            ty: place.y - at.y - local.y * sy
        };
    }
    readonly property bool sharing: miniBar !== null && !reduced && !expressive
    // The page's elements fade in early on the way, the mini player's copies fade out.
    readonly property real handIn: sharing ? Theme.motion.segment(p, 0.1, 0.5) : late
    readonly property real handOut: 1 - Theme.motion.segment(p, 0.15, 0.55)
    // What has no counterpart in the mini player fades in late and rises a little.
    readonly property real late: Theme.motion.segment(p, 0.55, 1)
    // The page's parts that pair up with the mini player's, in their elements, and
    // where those parts end up.
    readonly property rect titleLocal: Qt.rect(header.textItem.x, header.textItem.y,
                                               header.textItem.width, header.textItem.height)
    readonly property rect controlsLocal: controls.transportRect
    readonly property rect progressLocal: Qt.rect(progress.barItem.x, progress.barItem.y,
                                                  progress.barItem.width, progress.barItem.height)
    readonly property rect titleTo: Qt.rect(layout.header.x + titleLocal.x,
                                            layout.header.y + titleLocal.y,
                                            titleLocal.width, titleLocal.height)
    readonly property rect controlsTo: Qt.rect(layout.controls.x + controlsLocal.x,
                                               layout.controls.y + controlsLocal.y,
                                               controlsLocal.width, controlsLocal.height)
    readonly property rect progressTo: Qt.rect(layout.progress.x + progressLocal.x,
                                               layout.progress.y + progressLocal.y,
                                               progressLocal.width, progressLocal.height)
    // Identity once open (and without a mini player).
    readonly property var titleMove: flying && miniTitle
                                     ? moveOf(titleLocal, Qt.point(layout.header.x, layout.header.y),
                                              placed(miniTitleRect, titleTo, titleLocal, 0, p))
                                     : ({ sx: 1, sy: 1, tx: 0, ty: 0 })
    readonly property var controlsMove: flying && miniControls
                                        ? moveOf(controlsLocal,
                                                 Qt.point(layout.controls.x, layout.controls.y),
                                                 placed(miniControlsRect, controlsTo, controlsLocal, 1, p))
                                        : ({ sx: 1, sy: 1, tx: 0, ty: 0 })
    readonly property var progressMove: flying && miniSeek
                                        ? moveOf(progressLocal,
                                                 Qt.point(layout.progress.x, layout.progress.y),
                                                 placed(miniSeekRect, progressTo, progressLocal, 2, p))
                                        : ({ sx: 1, sy: 1, tx: 0, ty: 0 })
    onWidthChanged: if (opened || grow.value > 0) Qt.callLater(captureRects)
    onHeightChanged: if (opened || grow.value > 0) Qt.callLater(captureRects)

    // While the spring runs too: on its first frame and when closing bounces below 0,
    // the mini cover is already hidden and the flying one must still show.
    visible: grow.running || grow.value > 0.001 || fade.value > 0.001
    opacity: reduced ? fade.value : 1
    enabled: opened
    focus: opened
    ColorScope.scheme: colors.scheme
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("正在播放")

    Spring {
        id: grow
        target: page.opened ? 1 : 0
        spec: page.opened ? Theme.motion.slowSpatial : Theme.motion.defaultSpatial
        threshold: 0.001
    }
    Spring {
        id: fade
        target: page.opened ? 1 : 0
        spec: Theme.motion.defaultEffects
        threshold: 0.001
    }

    PlayerPalette {
        id: colors
        palette: Theme.coverPalette
        dark: App.settings.playerTheme === 0 ? Theme.dark : App.settings.playerTheme === 1
        // The Material style: the cover's own color in the chosen palette style, as the
        // mini and side players (CoverColors).
        coverSeed: page.expressive
        variant: page.expressive ? [Theme.Variant.TonalSpot, Theme.Variant.Vibrant, Theme.Variant.Expressive, Theme.Variant.Fidelity][
                                       Math.max(0, Math.min(3, App.settings.playerColorStyle))]
                                 : -1
    }

    Timer {
        id: settle
        interval: 900
        onTriggered: page.animateLayout = false
    }
    Timer {
        id: hideTimer
        interval: Math.max(1, App.settings.playerAutoHide) * 1000
        onTriggered: {
            if (!page.canHide)
                return;
            page.chromeHidden = true;
            // Keys come to the page first while hidden: the first one only wakes.
            if (!InputMode.keyboard)
                page.forceActiveFocus();
        }
    }
    // The focus goes into the panel that opens, and back to its button when it closes.
    property int lastSheet: 0
    onSheetChanged: {
        const before = lastSheet === 1 ? queueSheet : lastSheet === 2 ? settingsSheet : null;
        const opener = expressiveItem ? (lastSheet === 1 ? expressiveItem.queueButton
                                                         : expressiveItem.settingsButton)
                                      : lastSheet === 1 ? queueButton : settingsButton;
        lastSheet = sheet;
        if (sheet === 1) {
            queuePanel.list.forceActiveFocus(Qt.OtherFocusReason);
        } else if (sheet === 2) {
            settingsSheet.used = true;
            settingsSheet.closeButton.forceActiveFocus(Qt.OtherFocusReason);
        } else if (before) {
            for (let it = appWindow.activeFocusItem; it; it = it.parent) {
                if (it === before) {
                    opener.forceActiveFocus(Qt.OtherFocusReason);
                    break;
                }
            }
        }
    }
    // A new place for the controls or the title glides there.
    Connections {
        target: App.settings
        function onPlayerControlsChanged() {
            page.animateLayout = true;
            settle.restart();
            page.controlsPlace = App.settings.playerControls;
        }
        function onPlayerHeaderChanged() {
            page.animateLayout = true;
            settle.restart();
            page.headerPlace = App.settings.playerHeader;
        }
    }
    onCanHideChanged: if (canHide)
                          hideTimer.restart()
                      else
                          chromeHidden = false

    // --- Layout ----------------------------------------------------------------------

    readonly property real topHeight: 48
    // The bottom row: the view buttons on the right, the title on the left when it is
    // there. With the controls at the bottom it is a bar about the mini player's
    // height: the controls in the middle over the progress bar (§1.2).
    readonly property real barHeight: 88
    readonly property real bottomRowHeight: controlsAtBottom ? barHeight : 64
    readonly property real rowCenter: height - bottomRowHeight / 2
    readonly property real contentTop: topHeight + 8
    readonly property real contentBottom: height - bottomRowHeight - 8
    readonly property real sideMargin: wide ? 48 : 24
    // Heights under the cover: header, progress, controls and the gaps between.
    readonly property real headerHeight: 64
    readonly property real progressHeight: 44
    readonly property real controlsHeight: 76
    readonly property real stackExtra: {
        const parts = [];
        if (!headerAtBottom)
            parts.push(headerHeight);
        if (!controlsAtBottom)
            parts.push(progressHeight + 8 + controlsHeight);
        return parts.length === 0 ? 0 : 28 + parts.reduce((a, b) => a + b, 0) + 16 * (parts.length - 1);
    }
    readonly property real stackBottom: contentBottom
    // The bottom bar's middle: controls over the progress bar.
    readonly property real barCenterWidth: Math.min(560, width * 0.42)
    readonly property real barCenterX: (width - barCenterWidth) / 2
    // Two columns share a centered group that grows with the window, up to 1800; the
    // cover column takes 45% of it.
    readonly property real groupWidth: Math.min(width - 2 * sideMargin, 1800)
    readonly property real groupX: (width - groupWidth) / 2
    readonly property real leftWidth: groupWidth * 0.45
    // The largest cover (also its decode size).
    readonly property real maxCover: 720
    readonly property real minStackWidth: 360

    // Rects of the blocks for the current view and width.
    readonly property var layout: {
        const availH = stackBottom - contentTop;
        if (view === 0 || (wide && view > 0)) {
            const columnW = view === 0 ? width : leftWidth;
            const columnX = view === 0 ? 0 : groupX;
            const size = Math.max(120, Math.min(maxCover, height * (view === 0 ? 0.55 : 0.52),
                                                columnW - (view === 0 ? 2 * sideMargin : sideMargin),
                                                availH - stackExtra));
            const x = columnX + (columnW - size) / 2;
            const y = contentTop + Math.max(0, (availH - size - stackExtra) / 2);
            const below = y + size + 28;
            const progressY = headerAtBottom ? below : below + headerHeight + 16;
            // The title in the bottom row: up to the bar's middle or the view buttons.
            const headerRight = controlsAtBottom ? barCenterX - 24
                                                 : width - sideMargin - viewButtons.width - 24;
            // The header, progress and controls under a small cover keep a usable width.
            const stackW = Math.max(size, Math.min(minStackWidth, columnW - 2 * sideMargin));
            const stackX = columnX + (columnW - stackW) / 2;
            const panelX = groupX + leftWidth + sideMargin / 2;
            return {
                cover: Qt.rect(x, y, size, size),
                header: headerAtBottom ? Qt.rect(sideMargin, rowCenter - 28,
                                                 Math.max(120, headerRight - sideMargin), 56)
                                       : Qt.rect(stackX, below, stackW, headerHeight),
                progress: controlsAtBottom ? Qt.rect(barCenterX, height - 30, barCenterWidth, 20)
                                           : Qt.rect(stackX, progressY, stackW, progressHeight),
                controls: controlsAtBottom ? Qt.rect((width - Math.min(360, barCenterWidth)) / 2,
                                                     height - barHeight + 6,
                                                     Math.min(360, barCenterWidth), 52)
                                           : Qt.rect(stackX, progressY + progressHeight + 8,
                                                     stackW, controlsHeight),
                panel: Qt.rect(panelX, contentTop, groupX + groupWidth - panelX,
                               stackBottom - contentTop)
            };
        }
        // Narrow lyrics or queue: a header row, the panel, the controls at the bottom.
        const controlsY = contentBottom - controlsHeight;
        const progressY = controlsY - progressHeight - 4;
        const controlsW = Math.min(width - 2 * sideMargin, 420);
        return {
            cover: Qt.rect(sideMargin, contentTop, 64, 64),
            header: Qt.rect(sideMargin + 64 + 16, contentTop, width - 2 * sideMargin - 80, 64),
            progress: Qt.rect(sideMargin, progressY, width - 2 * sideMargin, progressHeight),
            controls: Qt.rect((width - controlsW) / 2, controlsY, controlsW, controlsHeight),
            panel: Qt.rect(sideMargin, contentTop + 64 + 24, width - 2 * sideMargin,
                           progressY - contentTop - 64 - 40)
        };
    }

    // A side panel's place: the right of the page, between the top row and the bottom
    // row.
    // In the Material style: solid, in the page's tray tone (player-expressive.md §2.4).
    component Sheet: PlayerSheet {
        width: Math.min(440, page.width - 2 * (page.expressiveItem ? 24 : page.sideMargin))
        x: page.width - (page.expressiveItem ? 24 : page.sideMargin / 2) - width
        y: page.expressiveItem ? page.expressiveItem.contentTop : page.contentTop
        height: (page.expressiveItem ? page.expressiveItem.sheetBottom : page.contentBottom) - y
        color: page.expressiveItem ? page.expressiveItem.sheetColor
                                   : Qt.alpha(ColorScope.scheme.surface, 0.8)
        onCloseRequested: page.sheet = 0
    }

    // An item that glides to `to` (springs) while views switch, and follows at once
    // otherwise (resizing).
    component Glide: Item {
        id: glide
        property rect to
        property bool animate
        function follow(): void {
            if (animate) {
                gx.target = to.x;
                gy.target = to.y;
                gw.target = to.width;
                gh.target = to.height;
            } else {
                gx.snapTo(to.x);
                gy.snapTo(to.y);
                gw.snapTo(to.width);
                gh.snapTo(to.height);
            }
        }
        x: gx.value
        y: gy.value
        width: gw.value
        height: gh.value
        onToChanged: follow()
        Component.onCompleted: follow()
        Spring {
            id: gx
            spec: Theme.motion.defaultSpatial
        }
        Spring {
            id: gy
            spec: Theme.motion.defaultSpatial
        }
        Spring {
            id: gw
            spec: Theme.motion.defaultSpatial
        }
        Spring {
            id: gh
            spec: Theme.motion.defaultSpatial
        }
    }

    // --- The growing panel -----------------------------------------------------------

    readonly property real p: reduced ? 1 : grow.value
    readonly property rect panelRect: miniBar && !reduced
                                      ? Qt.rect(Theme.motion.lerp(miniBarRect.x, 0, p),
                                                Theme.motion.lerp(miniBarRect.y, 0, p),
                                                Theme.motion.lerp(miniBarRect.width, width, p),
                                                Theme.motion.lerp(miniBarRect.height, height, p))
                                      : Qt.rect(0, 0, width, height)
    // From the capsule's corners (round ends) to the window's, never rounder than half
    // the panel's height.
    readonly property real panelRadius: Math.min(panelRect.height / 2,
                                                 Theme.motion.lerp(miniBarRadius,
                                                                   appWindow.cornerRadius,
                                                                   Math.min(1, Math.max(0, p))))

    // The capsule floats: the panel keeps its shadow while it is still small.
    Elevation {
        x: panel.x
        y: panel.y
        width: panel.width
        height: panel.height
        radius: page.panelRadius
        level: Theme.elevation.level1
        opacity: page.miniBar && !page.reduced
                 ? panel.opacity * (1 - Theme.motion.segment(page.p, 0.1, 0.4)) : 0
    }

    Item {
        id: panel
        x: page.panelRect.x
        y: page.panelRect.y
        width: Math.max(0, page.panelRect.width)
        height: Math.max(0, page.panelRect.height)
        clip: page.p < 0.999
        // It starts as the capsule (same place, color and corners) and is opaque almost
        // at once, so the capsule seems to open up; the capsule's own buttons fade under
        // it over that first stretch, and come back the same way when it closes.
        opacity: page.miniBar && !page.reduced ? Theme.motion.segment(page.p, 0, 0.08) : 1

        PlayerBackground {
            anchors.fill: parent
            visible: !page.expressive
            radius: page.panelRadius
            colors: colors
            flowing: App.player.status === Playback.Playing && !page.expressive
        }
        // The Material style: one color from the cover (player-expressive.md §1).
        Rectangle {
            anchors.fill: parent
            visible: page.expressive
            radius: page.panelRadius
            color: colors.scheme.primaryContainer
        }
        // The capsule's color, fading into the page's background.
        Rectangle {
            anchors.fill: parent
            radius: page.panelRadius
            color: page.miniBarColor
            opacity: page.miniBar && !page.reduced ? 1 - Theme.motion.segment(page.p, 0, 0.5) : 0
            visible: opacity > 0
        }

        // The page's content at its final size, revealed by the panel.
        Item {
            id: content
            x: -panel.x
            y: -panel.y + (page.miniBar || page.reduced ? 0 : 48 * (1 - page.p))
            width: page.width
            height: page.height

            // Pointer movement anywhere shows the controls again.
            HoverHandler {
                onPointChanged: {
                    const at = point.scenePosition;
                    if (Math.abs(at.x - page.lastPointer.x) + Math.abs(at.y - page.lastPointer.y) < 2)
                        return;
                    page.lastPointer = at;
                    page.wake();
                }
            }

            Glide {
                id: coverBox
                visible: !page.expressive
                to: page.layout.cover
                animate: page.animateLayout
                PlayerCover {
                    id: cover
                    anchors.fill: parent
                    colors: colors
                    opacity: page.flying ? 0 : 1
                }
            }

            Glide {
                id: panelBox
                to: page.layout.panel
                animate: page.animateLayout
                opacity: Theme.motion.segment(page.p, 0.65, 1) * panelFade.value
                visible: opacity > 0 && !page.expressive

                Spring {
                    id: panelFade
                    target: page.view > 0 ? 1 : 0
                    spec: Theme.motion.defaultEffects
                }
                LyricsView {
                    id: lyricsView
                    anchors.fill: parent
                    colors: colors
                    positionMs: clock.positionMs
                    live: page.visible && page.view === 1
                    visible: page.view === 1
                    onCopyRequested: songInfo.open()
                }
            }

            // Clicking the empty bottom row, where the mini player was, closes the page
            // (§2.3); hovering it shows a very faint band with a chevron as the hint.
            Item {
                id: closeBand
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: page.bottomRowHeight
                visible: !page.expressive
                opacity: Theme.motion.segment(page.p, 0.55, 1)

                readonly property bool hinted: bandHover.hovered && !page.overControls
                                               && !page.chromeHidden

                HoverHandler {
                    id: bandHover
                    cursorShape: page.overControls ? Qt.ArrowCursor : Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: if (!page.overControls)
                                  page.close()
                }
                Spring {
                    id: bandHint
                    target: closeBand.hinted ? 1 : 0
                    spec: Theme.motion.fastEffects
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height
                    bottomLeftRadius: page.panelRadius
                    bottomRightRadius: page.panelRadius
                    opacity: bandHint.value
                    visible: opacity > 0.01
                    gradient: Gradient {
                        GradientStop {
                            position: 0
                            color: Qt.alpha(colors.content, 0)
                        }
                        GradientStop {
                            position: 1
                            color: Qt.alpha(colors.content, 0.035)
                        }
                    }
                }
                Icon {
                    anchors.centerIn: parent
                    visible: !page.controlsAtBottom && opacity > 0.01
                    opacity: 0.6 * bandHint.value
                    glyph: Symbols.expand_more
                    color: colors.contentVariant
                }
            }

            // Header, progress and controls. With the mini player they fly from its
            // title, buttons and seek bar; the rest (and all without it) fades in late
            // and rises a little.
            Item {
                id: chrome
                anchors.fill: parent
                visible: !page.expressive
                transform: Translate {
                    y: page.sharing ? 0 : 16 * (1 - page.late)
                }

                Glide {
                    to: page.layout.header
                    animate: page.animateLayout
                    opacity: page.handIn
                    SongHeader {
                        id: header
                        anchors.fill: parent
                        transform: [
                            Scale {
                                xScale: page.titleMove.sx
                                yScale: page.titleMove.sy
                            },
                            Translate {
                                x: page.titleMove.tx
                                y: page.titleMove.ty
                            }
                        ]
                        colors: colors
                        compact: (!page.wide && page.view > 0) || page.headerAtBottom
                        moreAfterText: page.headerAtBottom
                        titleSize: Math.max(22, Math.min(32, page.layout.cover.width / 20))
                        onCommentsRequested: page.commentsRequested()
                        onSongRequested: page.songRequested()
                        onArtistRequested: key => page.artistRequested(key)
                        onAlbumRequested: key => page.albumRequested(key)
                        onPodcastRequested: key => page.podcastRequested(key)
                        onAddToPlaylistRequested: (key, title) => page.addToPlaylistRequested(key,
                                                                                              title)
                        onCopyRequested: songInfo.open()
                        onTogetherRequested: page.togetherRequested()
                    }
                }

                // What hides while playing.
                Item {
                    id: hideable
                    anchors.fill: parent
                    opacity: hidden.value
                    visible: opacity > 0
                    enabled: !page.chromeHidden

                    Spring {
                        id: hidden
                        target: page.chromeHidden ? 0 : 1
                        spec: page.chromeHidden ? Theme.motion.slowEffects : Theme.motion.fastEffects
                        Component.onCompleted: snapTo(1)
                    }

                    Glide {
                        to: page.layout.progress
                        animate: page.animateLayout
                        opacity: page.handIn
                        PlayerProgress {
                            id: progress
                            anchors.fill: parent
                            transform: [
                                Scale {
                                    xScale: page.progressMove.sx
                                    yScale: page.progressMove.sy
                                },
                                Translate {
                                    x: page.progressMove.tx
                                    y: page.progressMove.ty
                                }
                            ]
                            colors: colors
                            positionMs: clock.positionMs
                            timesBeside: page.controlsAtBottom
                        }
                        HoverHandler {
                            id: progressHover
                        }
                    }
                    Glide {
                        to: page.layout.controls
                        animate: page.animateLayout
                        opacity: page.handIn
                        PlayerControls {
                            id: controls
                            anchors.fill: parent
                            transform: [
                                Scale {
                                    xScale: page.controlsMove.sx
                                    yScale: page.controlsMove.sy
                                },
                                Translate {
                                    x: page.controlsMove.tx
                                    y: page.controlsMove.ty
                                }
                            ]
                            colors: colors
                            compact: page.controlsAtBottom
                            onLoginRequested: page.loginRequested()
                        }
                        HoverHandler {
                            id: controlsHover
                        }
                    }

                    // The bottom row: volume, lyrics, the queue and the display settings.
                    Row {
                        id: viewButtons
                        opacity: page.late
                        anchors.right: parent.right
                        anchors.rightMargin: page.sideMargin - 10
                        y: rowY.value - height / 2
                        spacing: 4

                        Spring {
                            id: rowY
                            spec: Theme.motion.defaultSpatial
                            readonly property real to: page.rowCenter
                            onToChanged: page.animateLayout ? target = to : snapTo(to)
                            Component.onCompleted: snapTo(to)
                        }

                        HoverHandler {
                            id: rowHover
                        }

                        VolumeButton {
                            id: volume
                            colors: colors
                        }
                        PlayerButton {
                            checkable: true
                            checked: page.view === 1
                            color: colors.content
                            glyph: Symbols.lyrics
                            text: qsTr("歌词")
                            onClicked: page.setView(page.view === 1 ? 0 : 1)
                        }
                        PlayerButton {
                            id: queueButton
                            checkable: true
                            checked: page.sheet === 1
                            color: colors.content
                            glyph: Symbols.queue_music
                            text: qsTr("播放队列")
                            onClicked: page.sheet = page.sheet === 1 ? 0 : 1
                        }
                        PlayerButton {
                            id: settingsButton
                            visible: page.settingsContent !== null
                            checkable: true
                            checked: page.sheet === 2
                            color: colors.content
                            glyph: Symbols.tune
                            text: qsTr("显示设置")
                            onClicked: page.sheet = page.sheet === 2 ? 0 : 2
                        }
                    }

                    // The top row: close, full screen and the window buttons; moves the
                    // window like the shell's title bar row.
                    TitleBar {
                        opacity: page.late
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: page.topHeight
                        frame: page.appWindow.frame
                        windowControls: page.appWindow.titleBar !== TitleBarMode.System
                        windowButtons: page.appWindow.customFrame
                                       && page.appWindow.visibility !== Window.FullScreen
                        buttonTint: colors.content
                        leadingInset: page.appWindow.titleBar === TitleBarMode.Custom
                                      && page.appWindow.macOS ? 72 : 8

                        HoverHandler {
                            id: topHover
                        }

                        PlayerButton {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            color: colors.content
                            glyph: Symbols.expand_more
                            text: qsTr("关闭播放页")
                            onClicked: page.close()
                        }
                        PlayerButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: colors.content
                            iconSize: 22
                            glyph: page.appWindow.visibility === Window.FullScreen
                                   ? Symbols.fullscreen_exit : Symbols.fullscreen
                            text: page.appWindow.visibility === Window.FullScreen ? qsTr("退出全屏")
                                                                                  : qsTr("全屏")
                            onClicked: page.toggleFullScreen()
                        }
                    }
                }
            }

            // The Material style's whole layout; it fades in late (its parts have no
            // counterparts in the mini player; the cover flies).
            Loader {
                id: expressiveLoader
                anchors.fill: parent
                active: page.expressive
                opacity: page.late
                visible: opacity > 0
                sourceComponent: ExpressivePlayer {
                    colors: colors
                    appWindow: page.appWindow
                    view: page.view
                    sheet: page.sheet
                    positionMs: clock.positionMs
                    flying: page.flying
                    hasSettings: page.settingsContent !== null
                    animate: page.animateLayout
                    onCloseRequested: page.close()
                    onFullScreenRequested: page.toggleFullScreen()
                    onViewRequested: next => page.setView(next)
                    onSheetRequested: next => page.sheet = next
                    onArtistRequested: key => page.artistRequested(key)
                    onAlbumRequested: key => page.albumRequested(key)
                    onPodcastRequested: key => page.podcastRequested(key)
                    onSongRequested: page.songRequested()
                    onCommentsRequested: page.commentsRequested()
                    onAddToPlaylistRequested: (key, title) => page.addToPlaylistRequested(key, title)
                    onCopyRequested: songInfo.open()
                    onLoginRequested: page.loginRequested()
                    onTogetherRequested: page.togetherRequested()
                }
            }

            // The side panels (§8, §9.1): from the right, over the lyrics; see-through,
            // so display changes show behind. A click beside them or Esc closes them.
            MouseArea {
                anchors.fill: parent
                visible: page.sheet !== 0
                // The lyrics under it don't react while a panel is open.
                hoverEnabled: true
                onWheel: wheel => wheel.accepted = true
                onPressed: page.sheet = 0
            }
            Sheet {
                id: queueSheet
                title: qsTr("播放队列")
                shown: page.sheet === 1
                actions: IconButton {
                    visible: App.player.mode === Playback.Queue && App.player.queue.count > 0
                    glyph: Symbols.clear_all
                    text: qsTr("清空播放队列")
                    onClicked: clearQueueDialog.open()
                }
                PlayerQueue {
                    id: queuePanel
                    anchors.fill: parent
                    onArtistRequested: key => page.artistRequested(key)
                    onAlbumRequested: key => page.albumRequested(key)
                    onPodcastRequested: key => page.podcastRequested(key)
                    onAddToPlaylistRequested: (key, title) => page.addToPlaylistRequested(key, title)
                }
            }
            Sheet {
                id: settingsSheet
                // Loaded on first opening, then kept.
                property bool used: false
                title: qsTr("显示设置")
                shown: page.sheet === 2

                Flickable {
                    id: settingsFlick
                    anchors.fill: parent
                    clip: true
                    contentHeight: settingsLoader.height + 12
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    // Tabbing to a row out of view scrolls it in.
                    Connections {
                        target: page.appWindow
                        enabled: page.sheet === 2
                        function onActiveFocusItemChanged() {
                            const item = page.appWindow.activeFocusItem;
                            let inside = false;
                            for (let it = item; it && !inside; it = it.parent)
                                inside = it === settingsScope;
                            if (!inside)
                                return;
                            const r = item.mapToItem(settingsFlick.contentItem, 0, 0, item.width,
                                                     item.height);
                            const flick = settingsFlick;
                            if (r.y < flick.contentY)
                                flick.contentY = Math.max(0, r.y - 16);
                            else if (r.y + r.height > flick.contentY + flick.height)
                                flick.contentY = Math.min(flick.contentHeight - flick.height,
                                                          r.y + r.height + 16 - flick.height);
                        }
                    }

                    FocusScope {
                        id: settingsScope
                        x: 16
                        width: settingsFlick.width - 32
                        height: settingsLoader.height
                        Loader {
                            id: settingsLoader
                            width: parent.width
                            active: settingsSheet.used
                            sourceComponent: page.settingsContent
                            onLoaded: item.width = Qt.binding(() => width)
                        }
                    }
                }
            }

            // While hidden, the first click only shows the controls, and the pointer is
            // hidden. Setting and resetting an item's cursor takes effect at once, even
            // with the pointer still; a handler's cursor would wait for the next move.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: page.chromeHidden ? Qt.AllButtons : Qt.NoButton
                cursorShape: page.chromeHidden ? Qt.BlankCursor : undefined
                onPressed: page.wake()
            }
        }
    }

    Item {
        anchors.fill: parent
        anchors.bottomMargin: page.bottomRowHeight
        Snackbar {
            id: snackbar
            anchors.bottom: parent.bottom
        }
    }

    // The mini player's title, buttons and seek bar on their way, fading out over the
    // page's (the originals are hidden meanwhile); under the flying cover, which the
    // title passes.
    component FlyingCopy: ShaderEffectSource {
        property rect from
        property rect to
        property int mode
        readonly property rect at: page.placed(from, to, from, mode, page.p)
        x: at.x
        y: at.y
        width: at.width
        height: at.height
        visible: page.flying && page.sharing && sourceItem !== null
        hideSource: page.flying && page.sharing
        opacity: page.handOut
        smooth: true
    }
    FlyingCopy {
        sourceItem: page.miniTitle
        from: page.miniTitleRect
        to: page.titleTo
        mode: 0
    }
    FlyingCopy {
        sourceItem: page.miniControls
        from: page.miniControlsRect
        to: page.controlsTo
        mode: 1
    }
    FlyingCopy {
        sourceItem: page.miniSeek
        from: page.miniSeekRect
        to: page.progressTo
        mode: 2
    }

    // The flying cover. From a shaped mini cover, a copy cut to that shape (and turned
    // as it was) fades into it over the first part of the way.
    CoverArt {
        visible: page.flying && page.flyShape >= 0
        x: flyingCover.x
        y: flyingCover.y
        width: flyingCover.width
        height: flyingCover.height
        rotation: page.flyTurn * (1 - Math.min(1, Math.max(0, flyingCover.t)))
        opacity: 1 - Theme.motion.segment(flyingCover.t, 0, 0.4)
        shape: page.flyShape
        coverKey: App.player.current.coverKey
        sourceSize: Qt.size(cover.decodeSize, cover.decodeSize)
    }
    CoverArt {
        id: flyingCover
        readonly property real t: page.p
        visible: page.flying
        opacity: (page.flyShape >= 0 ? Theme.motion.segment(t, 0, 0.4) : 1)
                 * (page.endShape >= 0 ? 1 - Theme.motion.segment(t, 0.6, 1) : 1)
        x: Theme.motion.lerp(page.miniCoverRect.x, page.coverRect.x, t)
        y: Theme.motion.lerp(page.miniCoverRect.y, page.coverRect.y, t)
        width: Theme.motion.lerp(page.miniCoverRect.width, page.coverRect.width, t)
        height: Theme.motion.lerp(page.miniCoverRect.height, page.coverRect.height, t)
        // The roundness relative to the size goes from the mini cover's to the page
        // cover's, so a circle morphs into a rounded square along the way instead of
        // turning square at the start (a radius lerp in pixels falls behind the size).
        radius: {
            const t = Math.min(1, Math.max(0, flyingCover.t));
            const from = page.miniCoverRect.width > 0 ? page.miniCoverRadius / page.miniCoverRect.width
                                                      : 0;
            const to = page.coverRect.width > 0 ? page.coverEndRadius / page.coverRect.width : 0;
            return width * Theme.motion.lerp(from, to, t);
        }
        coverKey: App.player.current.coverKey
        // The page cover's decode (cached), not a new one every frame of the flight.
        sourceSize: Qt.size(cover.decodeSize, cover.decodeSize)
    }

    // Landing on the Material style's shaped cover: a copy cut to its shape (and turned
    // as it is) fades in over the last part of the way.
    CoverArt {
        visible: page.flying && page.endShape >= 0
        x: flyingCover.x
        y: flyingCover.y
        width: flyingCover.width
        height: flyingCover.height
        rotation: page.endTurn * Math.min(1, Math.max(0, flyingCover.t))
        opacity: Theme.motion.segment(flyingCover.t, 0.6, 1)
        shape: page.endShape
        coverKey: App.player.current.coverKey
        sourceSize: Qt.size(cover.decodeSize, cover.decodeSize)
    }

    // The next song's cover, fetched and decoded ahead at the page's and the
    // background's sizes, so switching to it shows it at once.
    Repeater {
        model: [cover.decodeSize, 64]

        Image {
            required property int modelData
            visible: false
            asynchronous: true
            source: App.player.upcomingCoverKey !== ""
                    ? "image://cover/" + encodeURIComponent(App.player.upcomingCoverKey) : ""
            sourceSize: Qt.size(modelData, modelData)
        }
    }

    SongInfoDialog {
        id: songInfo
    }

    PlaybackClock {
        id: clock
        active: page.visible
    }
    Binding {
        target: App.lyrics
        property: "positionMs"
        value: clock.positionMs
        when: page.visible
    }

    // Single keys (§7); while hidden the first key only shows the controls (Space
    // still plays or pauses). They win over single-key shortcuts.
    Keys.onShortcutOverride: event => event.accepted = !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
        && (event.key === Qt.Key_Space || event.key === Qt.Key_L || event.key === Qt.Key_Q
            || event.key === Qt.Key_F)
    Keys.onPressed: event => {
        const hidden = page.chromeHidden;
        page.wake();
        switch (event.key) {
        case Qt.Key_Space:
            App.player.playPause();
            break;
        case Qt.Key_Escape:
            if (page.sheet !== 0)
                page.sheet = 0;
            else if (lyricsView.handBrowsing)
                lyricsView.stopBrowsing(true);
            else if (page.fullScreenByPlayer || page.appWindow.visibility === Window.FullScreen)
                page.toggleFullScreen();
            else
                page.close();
            break;
        default:
            if (hidden) {
                event.accepted = true;
                return;
            }
            switch (event.key) {
            case Qt.Key_Left:
                progress.seekBy(-5000);
                break;
            case Qt.Key_Right:
                progress.seekBy(5000);
                break;
            case Qt.Key_L:
                page.setView(page.view === 1 ? 0 : 1);
                break;
            case Qt.Key_Q:
                page.sheet = page.sheet === 1 ? 0 : 1;
                break;
            case Qt.Key_F:
                page.toggleFullScreen();
                break;
            default:
                return;
            }
        }
        event.accepted = true;
    }

    Dialog {
        id: clearQueueDialog
        title: qsTr("清空播放队列？")
        supportingText: qsTr("正在播放的歌也会停下。")
        acceptText: qsTr("清空")
        rejectText: qsTr("取消")
        onAccepted: App.player.clearQueue()
    }
}
