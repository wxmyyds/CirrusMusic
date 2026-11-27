pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The one search input of the app, in the title bar row (docs/ui/pages/shell.md §3,
// search.md §2.1). Works like a browser's address bar: the keyboard focus stays in
// the field while the list under it (wider than the field) shows recent searches as
// chips and trending keywords in two columns (empty field), or "搜索「…」" and
// suggestions grouped by type (typing). ↑/↓ move between rows; with the field empty
// ←/→ move within a row (there is no text cursor to move then). Enter runs the
// picked item or searches the text, Delete removes a picked recent search, Esc closes
// the list, then clears the text, then hands the focus back to the page.
// A suggested artist, album or playlist opens (`opened`); a song searches its title.
SearchBar {
    id: field

    // Recent searches, newest first; [{keyword, description}]; and for the text,
    // [{song, kind, key, title}] (SearchModel).
    property list<string> history
    property var hotKeywords: []
    property var suggestions: []
    // The list is wanted: the field was focused or edited since the last search.
    property bool listWanted: false
    property int picked: -1
    // Kept while a recent search is removed, so the pick stays in place.
    property bool keepPicked: false
    // The field's x in the window, for how far the list may reach (set on opening).
    property real windowX: 0

    readonly property bool typing: text.trim() !== ""
    // Recent searches offered (the chips that fit two rows show).
    readonly property int historyCount: typing ? 0 : Math.min(history.length, 10)
    // What can be picked, in order: {type: "history" | "hot" | "query" | "suggestion",
    // text, note, rank, group (the type, on a group's first suggestion), song, kind,
    // key}. Recent searches first, then trending; or the query, then suggestions.
    readonly property var lines: {
        const lines = [];
        if (typing) {
            lines.push({
                type: "query",
                text: text.trim(),
                group: ""
            });
            const groups = [qsTr("歌单"), qsTr("专辑"), qsTr("歌手"), qsTr("单曲")];
            let last = "";
            for (const suggestion of suggestions) {
                // Collection.Kind: Playlist 0, Album 1, Artist 2.
                const group = suggestion.song ? groups[3] : groups[suggestion.kind];
                lines.push({
                    type: "suggestion",
                    text: suggestion.title,
                    group: group === last ? "" : group,
                    song: suggestion.song,
                    kind: suggestion.kind,
                    key: suggestion.key
                });
                last = group;
            }
            return lines;
        }
        history.slice(0, historyCount).forEach(query => lines.push({
                type: "history",
                text: query
            }));
        hotKeywords.slice(0, 10).forEach((hot, i) => lines.push({
                type: "hot",
                text: hot.keyword,
                note: hot.description,
                rank: i + 1
            }));
        return lines;
    }
    readonly property bool listShown: activeFocus && listWanted && lines.length > 0
    // 560 wide where the window has room, never narrower than the field.
    readonly property real listWidth: Math.max(width, Math.min(560, (Window.width || 1280)
                                                                    - windowX - 16))
    // Trending in two columns (1–5, 6–10) when they fit.
    readonly property bool hotColumns: listWidth >= 480

    signal submitted(string text)
    // A suggested artist, album or playlist (Collection.Kind and key).
    signal opened(int kind, string key)
    signal historyRemoved(string text)
    signal historyCleared
    // Esc on an empty field.
    signal dismissed

    function submit(value: string): void {
        value = value.trim();
        if (value === "")
            return;
        listWanted = false;
        picked = -1;
        text = value;
        submitted(value);
    }

    function run(line: var): void {
        if (line.type !== "suggestion" || line.song) {
            submit(line.text);
            return;
        }
        listWanted = false;
        picked = -1;
        opened(line.kind, line.key);
    }

    function removeHistory(index: int): void {
        keepPicked = true;
        historyRemoved(lines[index].text);
        keepPicked = false;
        picked = Math.min(picked, lines.length - 1);
    }

    // The item showing line `index`, or null.
    function lineItem(index: int): Item {
        if (index < 0)
            return null;
        if (typing)
            return suggestionRepeater.itemAt(index);
        return index < historyCount ? chipRepeater.itemAt(index)
                                    : hotRepeater.itemAt(index - historyCount);
    }
    function lineCentre(index: int): point {
        const item = lineItem(index);
        return item.mapToItem(lineColumn, item.width / 2, item.height / 2);
    }

    // Moves the pick a row (dy) or within the row (dx). Rows are where the items are:
    // a row of chips, a row of the trending columns.
    function move(dx: int, dy: int): void {
        if (typing) {
            picked = Math.max(-1, Math.min(lines.length - 1, picked + dy));
            return;
        }
        const items = [];
        for (let i = 0; i < lines.length; ++i) {
            const item = lineItem(i);
            if (item && item.shown)
                items.push({
                    index: i,
                    centre: lineCentre(i)
                });
        }
        if (items.length === 0)
            return;
        const current = picked >= 0 ? lineItem(picked) : null;
        if (!current || !current.shown) {
            if (dy > 0)
                picked = items[0].index;
            return;
        }
        const here = lineCentre(picked);
        const nearestX = row => row.reduce((best, item) => Math.abs(item.centre.x - here.x)
                                           < Math.abs(best.centre.x - here.x) ? item : best);
        if (dy !== 0) {
            const others = items.filter(item => dy > 0 ? item.centre.y > here.y + 4
                                                       : item.centre.y < here.y - 4);
            if (others.length === 0) {
                // Up from the first row: back to the text.
                if (dy < 0)
                    picked = -1;
                return;
            }
            const rowY = others.reduce((best, item) => Math.abs(item.centre.y - here.y)
                                       < Math.abs(best - here.y) ? item.centre.y : best,
                                       others[0].centre.y);
            picked = nearestX(others.filter(item => Math.abs(item.centre.y - rowY) < 4)).index;
        } else {
            const row = items.filter(item => Math.abs(item.centre.y - here.y) < 4
                                     && (dx > 0 ? item.centre.x > here.x : item.centre.x < here.x));
            if (row.length > 0)
                picked = nearestX(row).index;
        }
    }

    // Keeps the picked line in view inside the scrolling list.
    function revealLine(index: int): void {
        const item = lineItem(index);
        if (!item)
            return;
        const y = item.mapToItem(lineColumn, 0, 0).y;
        if (index <= 0)
            flick.contentY = 0;
        else if (y < flick.contentY)
            flick.contentY = y;
        else if (y + item.height > flick.contentY + flick.height)
            flick.contentY = y + item.height - flick.height;
    }

    height: 48
    handlesEscape: false
    placeholderText: qsTr("搜索歌曲、歌手、专辑、歌单、播客")
    Accessible.name: qsTr("搜索")

    onActiveFocusChanged: {
        listWanted = activeFocus;
        picked = -1;
    }
    // A click on the field opens the list again, also when it kept the focus.
    onPressed: listWanted = true
    onTextEdited: {
        listWanted = true;
        picked = -1;
    }
    onLinesChanged: {
        if (!keepPicked)
            picked = -1;
    }
    onPickedChanged: revealLine(picked)
    onAccepted: {
        if (picked >= 0 && listShown)
            run(lines[picked]);
        else
            submit(text);
    }

    Keys.onDownPressed: {
        if (!listShown)
            listWanted = true;
        else
            move(0, 1);
    }
    Keys.onUpPressed: {
        if (listShown)
            move(0, -1);
    }
    Keys.onLeftPressed: event => {
        if (listShown && !typing && picked >= 0)
            move(-1, 0);
        else
            event.accepted = false;
    }
    Keys.onRightPressed: event => {
        if (listShown && !typing && picked >= 0)
            move(1, 0);
        else
            event.accepted = false;
    }
    Keys.onDeletePressed: event => {
        if (listShown && picked >= 0 && lines[picked].type === "history")
            removeHistory(picked);
        else
            event.accepted = false;
    }
    // SearchBar leaves Esc alone (handlesEscape: false); this handler runs after its.
    Keys.onEscapePressed: event => {
        event.accepted = true;
        if (listShown)
            listWanted = false;
        else if (length > 0)
            clear();
        else
            dismissed();
    }

    // The shortcut, while the field waits.
    Text {
        x: field.width - width - 16
        anchors.verticalCenter: parent.verticalCenter
        visible: !field.activeFocus && field.length === 0
        text: App.shortcuts.hints.search ?? ""
        color: Theme.color.outline
        font: Theme.type.labelMedium.font
    }

    // A group's title (recent, trending); recent searches can be cleared from it.
    component GroupTitle: Item {
        id: groupTitle

        property string text
        property bool clearable: false

        width: parent ? parent.width : 0
        height: 36
        Text {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            text: groupTitle.text
            color: Theme.color.onSurfaceVariant
            font: Theme.type.labelLarge.font
        }
        Button {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            visible: groupTitle.clearable
            variant: Md.Text
            size: Md.XSmall
            focusPolicy: Qt.NoFocus
            text: qsTr("清除")
            onClicked: field.historyCleared()
        }
    }

    // A line of the list, inset from its sides: a state layer whose corners round up
    // while it is hovered or picked (MD3E list and menu items); picking shows the
    // hover layer, as a menu's keyboard highlight does.
    component Line: T.AbstractButton {
        id: line

        property bool picked: false
        readonly property bool shown: true

        implicitHeight: 44
        leftInset: 8
        rightInset: 8
        leftPadding: 20
        rightPadding: 20
        focusPolicy: Qt.NoFocus
        Accessible.name: text

        HoverHandler {
            enabled: line.hoverEnabled
            cursorShape: Qt.PointingHandCursor
        }
        Spring {
            id: lineCorner
            target: line.picked || line.hovered || line.pressed ? Theme.shape.medium
                                                                : Theme.shape.extraSmall
            spec: Theme.motion.fastSpatial
            threshold: 0.1
        }

        background: StateLayer {
            radius: Math.max(0, lineCorner.value)
            hovered: line.hovered || line.picked
            pressed: line.pressed
            pressX: line.pressed ? line.pressX - line.leftInset : width / 2
            pressY: line.pressed ? line.pressY : height / 2
        }
    }

    // The list unfolds from the field and follows its height with a spring, so
    // suggestions arriving while typing don't make it jump; it fades out on close.
    // Opened and closed from `listShown`; a press outside the field and the list
    // closes it too (clicking the field, typing or ↓ opens it again). A docked MD3
    // search view: SurfaceContainerHigh, extra-large corners.
    onListShownChanged: listShown ? popup.open() : popup.close()

    T.Popup {
        id: popup

        property bool unfolded: false

        y: field.height + 4
        width: field.listWidth
        height: Math.max(0, sizeSpring.value)
        padding: 0
        topPadding: 8
        bottomPadding: 8
        // Never takes the focus: typing goes on in the field.
        focus: false
        closePolicy: T.Popup.CloseOnPressOutsideParent
        onAboutToShow: {
            field.windowX = field.mapToItem(null, 0, 0).x;
            unfolded = true;
        }
        onClosed: {
            unfolded = false;
            sizeSpring.snapTo(0);
            field.listWanted = false;
        }

        Spring {
            id: sizeSpring
            target: popup.unfolded ? popup.implicitContentHeight + popup.topPadding
                                     + popup.bottomPadding : 0
            spec: Theme.motion.standardFastSpatial
            threshold: 0.5
        }

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
        }

        background: Item {
            Elevation {
                anchors.fill: parent
                level: Theme.elevation.level2
                radius: Theme.shape.extraLarge
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.shape.extraLarge
                color: Theme.color.surfaceContainerHigh
            }
        }

        // Scrolls inside the window when there are many suggestions.
        contentItem: Flickable {
            id: flick
            implicitHeight: Math.min(lineColumn.implicitHeight,
                                     Math.max(160, (field.Window.height || 800) - 120))
            contentHeight: lineColumn.implicitHeight
            // What is past the springing height stays hidden.
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: lineColumn
                width: flick.width

                // Empty field: recent searches as input chips, at most two rows.
                Column {
                    width: parent.width
                    visible: field.historyCount > 0
                    bottomPadding: 8
                    GroupTitle {
                        text: qsTr("搜索历史")
                        clearable: true
                    }
                    Item {
                        x: 20
                        width: parent.width - 40
                        height: Math.min(chipFlow.implicitHeight, 2 * 32 + 8)
                        clip: true
                        Flow {
                            id: chipFlow
                            width: parent.width
                            spacing: 8
                            Repeater {
                                id: chipRepeater
                                model: field.typing ? [] : field.lines.slice(0, field.historyCount)
                                delegate: Chip {
                                    id: chip

                                    required property int index
                                    required property var modelData
                                    // Within the two rows the list shows.
                                    readonly property bool shown: y < 2 * 40

                                    removable: true
                                    maximumLabelWidth: 160
                                    focusPolicy: Qt.NoFocus
                                    highlighted: index === field.picked
                                    enabled: shown
                                    opacity: shown ? 1 : 0
                                    text: modelData.text
                                    Accessible.name: qsTr("搜索历史：%1").arg(text)
                                    onClicked: field.run(modelData)
                                    onRemoveClicked: field.removeHistory(chip.index)
                                }
                            }
                        }
                    }
                }

                // Empty field: trending, 1–5 then 6–10 beside them.
                Column {
                    width: parent.width
                    visible: !field.typing && field.lines.length > field.historyCount
                    GroupTitle {
                        text: qsTr("热门搜索")
                    }
                    Grid {
                        id: hotGrid
                        width: parent.width
                        flow: Grid.TopToBottom
                        rows: Math.max(1, field.hotColumns ? Math.ceil(hotRepeater.count / 2)
                                                           : hotRepeater.count)
                        Repeater {
                            id: hotRepeater
                            model: field.typing ? [] : field.lines.slice(field.historyCount)
                            delegate: Line {
                                id: hot

                                required property int index
                                required property var modelData

                                width: field.hotColumns ? hotGrid.width / 2 : hotGrid.width
                                picked: index + field.historyCount === field.picked
                                text: modelData.text
                                onClicked: field.run(modelData)

                                contentItem: Item {
                                    // Rank: the first three in primary.
                                    Text {
                                        id: rank
                                        width: 24
                                        anchors.verticalCenter: parent.verticalCenter
                                        horizontalAlignment: Text.AlignHCenter
                                        text: hot.modelData.rank ?? ""
                                        color: hot.modelData.rank <= 3 ? Theme.color.primary
                                                                       : Theme.color.onSurfaceVariant
                                        font: Fonts.tabular(Theme.type.titleSmall.font)
                                    }
                                    Text {
                                        x: rank.width + 12
                                        width: Math.min(implicitWidth, note.x - x - 12)
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: hot.text
                                        elide: Text.ElideRight
                                        color: Theme.color.onSurface
                                        font: Theme.type.bodyLarge.font
                                    }
                                    // The service's note; it gives way to the keyword.
                                    Text {
                                        id: note
                                        x: parent.width - width
                                        width: Math.min(implicitWidth, parent.width * 0.45)
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: hot.modelData.note ?? ""
                                        elide: Text.ElideRight
                                        color: Theme.color.onSurfaceVariant
                                        font: Theme.type.bodySmall.font
                                    }
                                }
                            }
                        }
                    }
                }

                // Typing: "搜索「…」", then suggestions with their type in a left column.
                Repeater {
                    id: suggestionRepeater
                    model: field.typing ? field.lines : []
                    delegate: Line {
                        id: line

                        required property int index
                        required property var modelData

                        width: lineColumn.width
                        picked: index === field.picked
                        text: modelData.type === "query" ? qsTr("搜索「%1」").arg(modelData.text)
                                                         : modelData.text
                        onClicked: field.run(modelData)

                        contentItem: Item {
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: line.modelData.type === "query"
                                glyph: Symbols.search
                                size: 20
                                color: Theme.color.onSurfaceVariant
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 48
                                visible: line.modelData.type !== "query"
                                text: line.modelData.group ?? ""
                                color: Theme.color.onSurfaceVariant
                                font: Theme.type.labelMedium.font
                            }
                            // The typed part in primary.
                            Text {
                                x: 48
                                width: parent.width - x
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                textFormat: Text.StyledText
                                text: {
                                    const escape = s => s.replace(/&/g, "&amp;")
                                                         .replace(/</g, "&lt;")
                                                         .replace(/>/g, "&gt;");
                                    if (line.modelData.type === "query")
                                        return escape(line.text);
                                    const title = line.modelData.text;
                                    const typed = field.text.trim();
                                    const at = title.toLowerCase().indexOf(typed.toLowerCase());
                                    if (at < 0 || typed === "")
                                        return escape(title);
                                    return escape(title.slice(0, at)) + "<font color=\""
                                        + Theme.color.primary + "\">"
                                        + escape(title.slice(at, at + typed.length)) + "</font>"
                                        + escape(title.slice(at + typed.length));
                                }
                                color: Theme.color.onSurface
                                font: Theme.type.bodyLarge.font
                            }
                        }
                    }
                }
            }
        }
    }
}
