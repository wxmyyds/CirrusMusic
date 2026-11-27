pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// Columns over time (docs/ui/pages/stats.md §2.3): one series, thin bars (at most 24 px,
// 4 px rounded tops, square at the baseline) growing from one baseline, one or two
// gridlines with their values on the left, the tallest bar labelled. Hovering a column
// or moving through them with the arrow keys (one Tab stop) shows its tooltip; clickable
// columns (Enter) emit activated(). Bar heights follow a spring when the values change.
//   BarChart { bars: model.bars; scaleTop: model.chartTop; grid: model.grid; … }
FocusScope {
    id: chart

    // [{label, ms, plays, future, clickable, tip}]
    property var bars: []
    // The value at the top of the plot.
    property real scaleTop: 1
    // [{ms, text}]
    property var grid: []
    property int peak: -1
    property string peakText
    property color barColor: ColorScope.scheme.primary
    property color textColor: ColorScope.scheme.onSurfaceVariant
    property color gridColor: Qt.alpha(textColor, 0.16)

    signal activated(int index)

    // The column under the pointer, and the one chosen with the keyboard.
    property int hoveredIndex: -1
    property int currentIndex: -1
    readonly property int shownIndex: hoveredIndex >= 0 ? hoveredIndex
                                                        : activeFocus ? currentIndex : -1

    readonly property real axisWidth: 56
    readonly property real labelBand: 20 // under the plot: the column labels
    readonly property real peakBand: 20  // above it: the tallest bar's value
    readonly property real plotX: axisWidth
    readonly property real plotWidth: Math.max(0, width - axisWidth)
    readonly property real plotHeight: Math.max(0, height - labelBand - peakBand)
    readonly property real slot: bars.length > 0 ? plotWidth / bars.length : 0

    implicitHeight: 200
    activeFocusOnTab: bars.length > 0
    Accessible.role: Accessible.Chart
    Accessible.name: shownIndex >= 0 ? bars[shownIndex].tip : ""

    function activate(index: int): void {
        if (index >= 0 && index < bars.length && bars[index].clickable)
            chart.activated(index);
    }
    function move(to: int): void {
        currentIndex = Math.max(0, Math.min(bars.length - 1, to));
    }

    onBarsChanged: {
        if (currentIndex >= bars.length)
            currentIndex = bars.length - 1;
    }
    onActiveFocusChanged: {
        if (activeFocus && currentIndex < 0)
            currentIndex = peak >= 0 ? peak : bars.length - 1;
    }
    Keys.onLeftPressed: move(currentIndex - 1)
    Keys.onRightPressed: move(currentIndex + 1)
    // Space is the chart's, before a single-key shortcut.
    Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Space
        && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Home) {
            move(0);
            event.accepted = true;
        } else if (event.key === Qt.Key_End) {
            move(bars.length - 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Space) {
            activate(currentIndex);
            event.accepted = true;
        }
    }

    // Gridlines and their values.
    Repeater {
        model: chart.grid
        delegate: Item {
            id: line
            required property var modelData
            readonly property real y0: chart.peakBand + chart.plotHeight
                                       - chart.plotHeight * modelData.ms / Math.max(1, chart.scaleTop)
            Rectangle {
                x: chart.plotX
                y: line.y0
                width: chart.plotWidth
                height: 1
                color: chart.gridColor
            }
            Text {
                x: 0
                width: chart.axisWidth - 8
                y: line.y0 - height / 2
                horizontalAlignment: Text.AlignRight
                text: line.modelData.text
                color: chart.textColor
                font: Theme.type.labelSmall.font
            }
        }
    }
    // The baseline.
    Rectangle {
        x: chart.plotX
        y: chart.peakBand + chart.plotHeight
        width: chart.plotWidth
        height: 1
        color: chart.gridColor
    }

    Repeater {
        id: columns
        // By count, not by the list: the same columns stay (and their springs move) when
        // only the values change.
        model: chart.bars.length
        delegate: Item {
            id: column
            required property int index
            readonly property var bar: chart.bars[index] ?? ({})
            readonly property bool shown: chart.shownIndex === index

            x: chart.plotX + index * chart.slot
            width: chart.slot
            height: chart.height
            Accessible.role: Accessible.ColumnHeader
            Accessible.name: bar.tip ?? ""

            Spring {
                id: grow
                target: chart.plotHeight * (column.bar.ms ?? 0) / Math.max(1, chart.scaleTop)
                spec: Theme.motion.defaultSpatial
                threshold: 0.5
                Component.onCompleted: snapTo(target)
            }

            // The column responds as a whole (hover, focus), not only its bar.
            Rectangle {
                id: hit
                x: 1
                y: chart.peakBand - 4
                width: parent.width - 2
                height: chart.plotHeight + 8
                radius: Theme.shape.small
                color: "transparent"
                StateLayer {
                    anchors.fill: parent
                    radius: parent.radius
                    color: chart.textColor
                    hovered: column.shown && chart.hoveredIndex === column.index
                    focused: column.shown && chart.activeFocus && chart.hoveredIndex < 0
                             && InputMode.keyboard
                    pressed: false
                }
                FocusRing {
                    shown: chart.activeFocus && chart.currentIndex === column.index
                           && InputMode.keyboard
                    containerTopLeftRadius: hit.radius
                    containerTopRightRadius: hit.radius
                    containerBottomRightRadius: hit.radius
                    containerBottomLeftRadius: hit.radius
                }
            }

            Rectangle {
                readonly property real h: Math.max(0, grow.value)
                width: Math.min(24, Math.max(2, column.width - 2))
                x: (column.width - width) / 2
                height: h
                y: chart.peakBand + chart.plotHeight - h
                visible: h >= 1
                color: chart.barColor
                topLeftRadius: Math.min(4, h)
                topRightRadius: Math.min(4, h)
            }
            // Days still to come: a dot on the baseline.
            Rectangle {
                visible: column.bar.future === true
                width: 4
                height: 4
                radius: 2
                x: (column.width - width) / 2
                y: chart.peakBand + chart.plotHeight - 6
                color: Qt.alpha(chart.textColor, 0.38)
            }
            Text {
                visible: chart.peak === column.index && chart.peakText !== ""
                anchors.horizontalCenter: parent.horizontalCenter
                y: chart.peakBand + chart.plotHeight - grow.value - height - 2
                text: chart.peakText
                color: chart.textColor
                font: Theme.type.labelMedium.font
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: chart.peakBand + chart.plotHeight + 4
                text: column.bar.label ?? ""
                color: chart.textColor
                font: Theme.type.labelSmall.font
            }

            ToolTip {
                parent: column
                delay: 0
                visible: column.shown && (column.bar.tip ?? "") !== ""
                text: column.bar.tip ?? ""
            }
        }
    }

    HoverHandler {
        id: hover
        cursorShape: chart.hoveredIndex >= 0 && (chart.bars[chart.hoveredIndex]?.clickable ?? false)
                     ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPointChanged: chart.hoveredIndex = hovered ? chart.indexAt(point.position.x) : -1
        onHoveredChanged: {
            if (!hovered)
                chart.hoveredIndex = -1;
        }
    }
    TapHandler {
        onTapped: eventPoint => chart.activate(chart.indexAt(eventPoint.position.x))
    }

    function indexAt(x: real): int {
        if (slot <= 0 || x < plotX)
            return -1;
        const i = Math.floor((x - plotX) / slot);
        return i >= 0 && i < bars.length ? i : -1;
    }
}
