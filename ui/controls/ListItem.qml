pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 list item (one to three lines). `text` is the headline. Leading/trailing take
// a glyph or any component (cover, avatar, Switch...). `highlighted` is the selected
// state. `segmented` turns on the MD3E per-state corner rounding for gap lists.
// `first` / `last` round the outer corners of a list's ends by `outerRadius`, for
// segmented lists and for plain lists in a rounded container, so the state layer
// and ripple follow the container's corners.
// `supportingMaximumLines` above 2 lets long supporting text wrap and the item grow.
//   ListItem { text: song.title; supportingText: song.artist; leadingGlyph: Symbols.music_note }
T.ItemDelegate {
    id: control

    property string supportingText
    property string overline
    property string trailingText
    property string leadingGlyph
    property string trailingGlyph
    // A small mark after the supporting text (16 px, the supporting color), e.g. a song a
    // 心动模式 mix put in a queue; `supportingGlyphToolTip` names it on hover.
    property string supportingGlyph
    property string supportingGlyphToolTip
    property Component leading
    property Component trailing
    property bool segmented: false
    property bool first: false
    property bool last: false
    property real outerRadius: Theme.shape.large
    property color containerColor: "transparent"
    // 0 picks from content: 2 with supporting text, 3 with an overline or two-line support.
    property int lines: 0
    property int supportingMaximumLines: 1
    readonly property alias leadingItem: leadingLoader.item
    readonly property alias trailingItem: trailingLoader.item

    readonly property int lineCount: lines > 0 ? lines
                                   : overline !== "" || supportingText !== ""
                                     && supportingMaximumLines > 1 ? 3
                                   : supportingText !== "" ? 2 : 1
    readonly property color headlineColor: !enabled
        ? Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContent)
        : Theme.mix(ColorScope.scheme.onSurface, ColorScope.scheme.onSecondaryContainer, selection.value)
    readonly property color mutedColor: !enabled
        ? Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContent)
        : Theme.mix(ColorScope.scheme.onSurfaceVariant, ColorScope.scheme.onSecondaryContainer,
                    selection.value)
    readonly property real cornerTarget: !segmented ? 0
                                       : highlighted || down ? Theme.shape.large
                                       : hovered || visualFocus ? Theme.shape.medium
                                       : Theme.shape.extraSmall

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    // Taller when the supporting text wraps past what the line count allows for.
    implicitHeight: Math.max([56, 72, 88][lineCount - 1],
                             implicitContentHeight + topPadding + bottomPadding)
    leftPadding: 16
    rightPadding: 16
    topPadding: 10
    bottomPadding: 10
    spacing: 12
    font: Theme.type.bodyLarge.font

    Spring {
        id: selection
        target: control.highlighted ? 1 : 0
        spec: Theme.motion.defaultEffects
    }
    Spring {
        id: corner
        target: control.cornerTarget
        spec: Theme.motion.fastSpatial
        threshold: 0.1
    }

    contentItem: Item {
        implicitWidth: (leadingLoader.active ? leadingLoader.implicitWidth + control.spacing : 0)
                       + Math.max(headline.implicitWidth, supportRow.implicitWidth)
                       + (trailingLoader.active ? trailingLoader.implicitWidth
                                                  + control.spacing : 0)
        implicitHeight: textColumn.implicitHeight

        Loader {
            id: leadingLoader
            opacity: control.enabled || control.leading === null ? 1
                                                                 : Theme.state.disabledContent
            anchors.verticalCenter: control.lineCount === 3 ? undefined : parent.verticalCenter
            y: 0
            active: control.leading !== null || control.leadingGlyph !== ""
            sourceComponent: control.leading ?? leadingGlyphComponent
        }

        Column {
            id: textColumn
            x: leadingLoader.active ? leadingLoader.width + control.spacing : 0
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - x - (trailingLoader.active ? trailingLoader.width
                                                               + control.spacing : 0)

            Text {
                visible: control.overline !== ""
                width: parent.width
                text: control.overline
                font: Theme.type.labelSmall.font
                color: control.mutedColor
                elide: Text.ElideRight
            }
            Text {
                id: headline
                width: parent.width
                text: control.text
                font: control.font
                color: control.headlineColor
                elide: Text.ElideRight
            }
            Row {
                id: supportRow
                visible: control.supportingText !== ""
                width: parent.width
                spacing: 4

                Text {
                    id: support
                    width: !mark.visible ? parent.width
                         : Math.min(implicitWidth, parent.width - mark.width - parent.spacing)
                    text: control.supportingText
                    font: Theme.type.bodyMedium.font
                    color: control.mutedColor
                    elide: Text.ElideRight
                    wrapMode: control.supportingMaximumLines > 1 ? Text.Wrap : Text.NoWrap
                    maximumLineCount: control.supportingMaximumLines
                }
                Icon {
                    id: mark
                    visible: control.supportingGlyph !== ""
                    y: (Math.min(support.height, support.font.pixelSize * 1.5) - height) / 2
                    glyph: control.supportingGlyph
                    size: 16
                    color: control.mutedColor

                    // The tooltip is made on first hover and kept, so it can fade out.
                    property bool hoveredOnce: false
                    HoverHandler {
                        id: markHover
                        onHoveredChanged: if (hovered)
                                              mark.hoveredOnce = true
                    }
                    Loader {
                        active: control.supportingGlyphToolTip !== "" && mark.hoveredOnce
                        sourceComponent: ToolTip {
                            parent: mark
                            text: control.supportingGlyphToolTip
                            visible: markHover.hovered
                        }
                    }
                }
            }
        }

        Loader {
            id: trailingLoader
            opacity: control.enabled || control.trailing === null ? 1
                                                                  : Theme.state.disabledContent
            x: parent.width - width
            anchors.verticalCenter: control.lineCount === 3 ? undefined : parent.verticalCenter
            active: control.trailing !== null || control.trailingGlyph !== ""
                    || control.trailingText !== ""
            sourceComponent: control.trailing ?? (control.trailingText !== ""
                                                  ? trailingTextComponent
                                                  : trailingGlyphComponent)
        }
    }

    Component {
        id: leadingGlyphComponent
        Icon {
            glyph: control.leadingGlyph
            color: control.mutedColor
        }
    }
    Component {
        id: trailingGlyphComponent
        Icon {
            glyph: control.trailingGlyph
            color: control.mutedColor
        }
    }
    Component {
        id: trailingTextComponent
        Text {
            text: control.trailingText
            font: Theme.type.labelSmall.font
            color: control.mutedColor
        }
    }

    background: Rectangle {
        id: container
        implicitHeight: 56
        readonly property real inner: Math.max(0, corner.value)
        readonly property real topRadius: control.first ? Math.max(control.outerRadius, inner) : inner
        readonly property real bottomRadius: control.last ? Math.max(control.outerRadius, inner) : inner

        color: Theme.mix(control.containerColor, ColorScope.scheme.secondaryContainer, selection.value)
        topLeftRadius: topRadius
        topRightRadius: topRadius
        bottomLeftRadius: bottomRadius
        bottomRightRadius: bottomRadius

        StateLayer {
            anchors.fill: parent
            topLeftRadius: container.topRadius
            topRightRadius: container.topRadius
            bottomLeftRadius: container.bottomRadius
            bottomRightRadius: container.bottomRadius
            color: ColorScope.scheme.onSurface
            hovered: control.hovered && control.enabled
            pressed: control.down
            focused: control.visualFocus
            pressX: control.pressed ? control.pressX : width / 2
            pressY: control.pressed ? control.pressY : height / 2
        }

        FocusRing {
            shown: control.visualFocus
            offset: -5
            containerTopLeftRadius: container.topRadius
            containerTopRightRadius: container.topRadius
            containerBottomLeftRadius: container.bottomRadius
            containerBottomRightRadius: container.bottomRadius
        }
    }

    // A hand pointer wherever the control shows hover (controls.md §2).
    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
}
