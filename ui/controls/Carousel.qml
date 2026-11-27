pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// A multi-browse carousel (MD3; the sizes are androidx's CarouselDefaults and
// multiBrowseKeylineList): large items, then a medium and a small one at the trailing
// edge, which tell there is more. Items keep their height and change width as the row
// moves, so a delegate fills the width it gets (crop, don't scale). Near the end the
// small and the medium item move to the leading edge, and the last items get large too.
// One tab stop: ← → move the current item and bring it among the large ones, Enter or
// Space activates it. A horizontal wheel or touchpad swipe moves the row; the vertical
// wheel stays with the page. `back()` and `forward()` page.
T.Control {
    id: control

    property alias model: repeater.model
    // The control sets its x, width and height. It reads `index` and the model's roles
    // as in any view, and `largeWidth` here for what it decodes or lays out once.
    property alias delegate: repeater.delegate
    // What a large item would like; the real width (`largeWidth`) fills the row.
    property real preferredItemWidth: 196
    property real itemSpacing: 8
    property int currentIndex: 0
    readonly property int count: repeater.count

    readonly property real smallWidth: Math.max(40, Math.min(56, preferredItemWidth / 3))
    readonly property int largeCount: {
        const medium = (preferredItemWidth + smallWidth) / 2;
        return Math.max(1, Math.round((availableWidth - medium - smallWidth - 2 * itemSpacing)
                                      / (preferredItemWidth + itemSpacing)));
    }
    readonly property real largeWidth: Math.max(
        smallWidth, (availableWidth - 1.5 * smallWidth - (largeCount + 1) * itemSpacing)
                    / (largeCount + 0.5))
    readonly property real mediumWidth: (largeWidth + smallWidth) / 2

    // The row's position is the index of the first large item: 0 to `end`.
    readonly property real end: Math.max(0, count - largeCount)
    // Where it rests, and where it is.
    property real target: 0
    readonly property real position: spring.value
    readonly property bool canGoBack: target > 0.01
    readonly property bool canGoForward: target < end - 0.01
    // The current item shows the keyboard focus.
    readonly property bool currentShown: visualFocus

    signal activated(int index)

    function moveTo(first: real): void {
        target = Math.max(0, Math.min(end, first));
    }
    function back(): void {
        moveTo(Math.round(target) - largeCount);
    }
    function forward(): void {
        moveTo(Math.round(target) + largeCount);
    }
    // Brings an item among the large ones.
    function reveal(index: int): void {
        moveTo(Math.max(index - largeCount + 1, Math.min(index, Math.round(target))));
    }
    function itemAt(index: int): Item {
        return repeater.itemAt(index);
    }
    function select(index: int): void {
        if (count === 0)
            return;
        currentIndex = Math.max(0, Math.min(count - 1, index));
        reveal(currentIndex);
    }

    // Widths follow keylines: at offset `d` from the first large item an item is small
    // (-2), medium (-1), large (0 to n-1), medium (n) or small (n+1), nothing beyond,
    // and in between it is in between. The leading pair has the share `a` of its size
    // and the trailing pair the rest: `a` goes from 0 to 1 over two items near the end.
    function arrange(): void {
        const n = largeCount, p = position, gap = itemSpacing;
        const from = end >= 6 ? end - 4 : (end - 2) / 2;
        const a = end <= 0 ? 0 : end <= 2 ? Math.max(0, Math.min(1, p / end))
                                         : Math.max(0, Math.min(1, (p - from) / 2));
        const b = 1 - a;
        const width = k => k <= -3 || k >= n + 2 ? 0 : k === -2 ? a * smallWidth
                         : k === -1 ? a * mediumWidth : k < n ? largeWidth
                         : k === n ? b * mediumWidth : b * smallWidth;
        const share = k => k <= -3 || k >= n + 2 ? 0 : k < 0 ? a : k < n ? 1 : b;
        const widths = [], shares = [];
        let total = 0, gaps = 0, last = -1;
        for (let i = 0; i < count; ++i) {
            const k = Math.floor(i - p), t = i - p - k;
            widths.push(width(k) * (1 - t) + width(k + 1) * t);
            shares.push(share(k) * (1 - t) + share(k + 1) * t);
            total += widths[i];
            gaps += shares[i] * gap;
            if (widths[i] > 0.5)
                last = i;
        }
        // Enough items to fill the row: they do, exactly (the keylines leave a few
        // pixels over where an item they count on does not exist).
        let scale = 1;
        if (count >= n + 2 && total > 0 && last >= 0)
            scale = Math.max(1, (availableWidth - gaps + shares[last] * gap) / total);
        let x = 0;
        for (let i = 0; i < count; ++i) {
            const item = repeater.itemAt(i);
            if (!item)
                continue;
            const w = widths[i] * scale;
            item.x = x;
            item.y = 0;
            item.width = w;
            item.height = availableHeight;
            item.visible = w > 0.5;
            x += w + shares[i] * gap;
        }
    }

    implicitHeight: 200 + topPadding + bottomPadding
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.List

    onPositionChanged: arrange()
    onAvailableWidthChanged: arrange()
    onAvailableHeightChanged: arrange()
    onLargeWidthChanged: arrange()
    onCountChanged: {
        currentIndex = Math.max(0, Math.min(count - 1, currentIndex));
        Qt.callLater(arrange);
    }
    onEndChanged: moveTo(target)

    Keys.onLeftPressed: select(currentIndex - 1)
    Keys.onRightPressed: select(currentIndex + 1)
    // Space is the carousel's, before a single-key shortcut.
    Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Space && count > 0
        && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Home)
            select(0);
        else if (event.key === Qt.Key_End)
            select(count - 1);
        else if (event.key === Qt.Key_PageUp)
            select(currentIndex - largeCount);
        else if (event.key === Qt.Key_PageDown)
            select(currentIndex + largeCount);
        else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                  || event.key === Qt.Key_Space) && count > 0)
            activated(currentIndex);
        else
            return;
        event.accepted = true;
    }

    Spring {
        id: spring
        target: control.target
        spec: Theme.motion.defaultSpatial
    }

    // A swipe leaves the row between two items: it settles on the nearer one.
    Timer {
        id: settle
        interval: 160
        onTriggered: control.moveTo(Math.round(control.target))
    }
    WheelHandler {
        orientation: Qt.Horizontal
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            const step = event.pixelDelta.x !== 0
                       ? event.pixelDelta.x / (control.largeWidth + control.itemSpacing)
                       : event.angleDelta.x / 120;
            control.moveTo(control.target - step);
            settle.restart();
        }
    }

    contentItem: Item {
        clip: true
        Repeater {
            id: repeater
            onItemAdded: Qt.callLater(control.arrange)
        }
    }
}
