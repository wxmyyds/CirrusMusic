pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components

// Settings (docs/ui/pages/settings.md): one long page of groups, each its own file
// under settings/ built from the row types there; on wide windows an index of the
// groups on the left follows the scroll and jumps to a group. A new group is one line
// in `groups` below: the index picks it up.
PageScaffold {
    id: page

    required property AppShell shell

    readonly property real indexWidth: 220
    readonly property bool wide: width >= 720
    readonly property real contentX: wide ? 16 + indexWidth + 16 : 24
    readonly property real contentWidth: Math.min(width - contentX - 16, 720)
    // The groups in order, for the index. Use explicit object references: the page may
    // be asynchronously incubated, so observing children during construction can expose
    // a partial list to the index.
    property var sections: [accountGroup, playbackGroup, lyricsGroup, playerGroup,
                            appearanceGroup, windowGroup, shareGroup, storageGroup,
                            diagnosticsGroup, shortcutGroup, aboutGroup]
    // The group at the top of the view.
    property int current: 0

    function updateCurrent(): void {
        if (sections.length === 0)
            return;
        // At the end the last groups can't reach the top: the last one counts then.
        if (flick.contentY >= flick.contentHeight - flick.height - 1) {
            current = sections.length - 1;
            return;
        }
        let found = 0;
        for (let i = 0; i < sections.length; ++i) {
            if (sections[i].y <= flick.contentY + 24)
                found = i;
        }
        current = found;
    }

    function scrollTo(index: int): void {
        const group = sections[index];
        if (!group)
            return;
        scroll.to = Math.max(0, Math.min(group.y - 8, flick.contentHeight - flick.height));
        scroll.restart();
    }

    title: qsTr("设置")

    // The index: one Tab stop (the current group); ↑/↓ move, Enter goes there.
    Column {
        id: sectionIndex

        property int focusIndex: page.current

        function moveFocus(step: int): void {
            const next = Math.max(0, Math.min(page.sections.length - 1, focusIndex + step));
            (entries.itemAt(next) as Item)?.forceActiveFocus(Qt.TabFocusReason);
            focusIndex = next;
        }

        x: 16
        y: 8
        // Above the page-wide Flickable (declared after it for the Tab order).
        z: 1
        width: page.indexWidth
        visible: page.wide
        spacing: 2
        onActiveFocusChanged: if (!activeFocus) focusIndex = page.current
        Accessible.role: Accessible.List
        Accessible.name: qsTr("设置分组")

        Repeater {
            id: entries
            model: page.sections
            delegate: ListItem {
                id: entry
                required property int index
                required property SettingsGroup modelData
                width: parent.width
                implicitHeight: 52
                lines: 1
                segmented: true
                first: true
                last: true
                outerRadius: 26
                highlighted: index === page.current
                text: modelData.title
                // The group's icon on a disc of its static hue (SettingsGroup.hue).
                leading: Component {
                    Rectangle {
                        readonly property var hue: Theme.staticPalette[entry.modelData.hue]
                        width: 32
                        height: 32
                        radius: 16
                        color: hue.container
                        Icon {
                            anchors.centerIn: parent
                            glyph: entry.modelData.glyph
                            size: 20
                            fill: entry.highlighted ? 1 : 0
                            color: parent.hue.onContainer
                        }
                    }
                }
                focusPolicy: index === sectionIndex.focusIndex ? Qt.StrongFocus : Qt.ClickFocus
                Accessible.role: Accessible.ListItem
                onClicked: page.scrollTo(index)
                Keys.onUpPressed: sectionIndex.moveFocus(-1)
                Keys.onDownPressed: sectionIndex.moveFocus(1)
                Keys.onReturnPressed: page.scrollTo(index)
                Keys.onEnterPressed: page.scrollTo(index)
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: groups.implicitHeight + 48
        bottomMargin: PageInsets.bottom
        clip: true
        // Overshoots and springs back at the ends.
        boundsBehavior: Flickable.DragAndOvershootBounds
        onContentYChanged: page.updateCurrent()
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: groups
            x: page.contentX
            width: page.contentWidth
            topPadding: 8
            spacing: 32

            AccountSettings {
                id: accountGroup
                shell: page.shell
            }
            PlaybackSettings {
                id: playbackGroup
            }
            LyricsSettings {
                id: lyricsGroup
            }
            PlayerSettings {
                id: playerGroup
            }
            AppearanceSettings {
                id: appearanceGroup
            }
            WindowSettings {
                id: windowGroup
            }
            ShareSettings {
                id: shareGroup
            }
            StorageSettings {
                id: storageGroup
                shell: page.shell
            }
            DiagnosticsSettings {
                id: diagnosticsGroup
                shell: page.shell
            }
            ShortcutSettings {
                id: shortcutGroup
            }
            AboutSettings {
                id: aboutGroup
            }
        }
    }

    NumberAnimation {
        id: scroll
        target: flick
        property: "contentY"
        duration: Theme.motion.defaultSpatial.duration
        easing: Theme.motion.defaultSpatial.easing
    }
}
