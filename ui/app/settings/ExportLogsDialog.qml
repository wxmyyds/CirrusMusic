pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as D
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Picks the log files for a diagnostic package (docs/tasks/logging.md §7, logging-v2
// §5.2, §12): every file listed with its size and time; ticked are this run, the one
// before, and any with a mark or that did not end. The user may say what happened, and
// read the package's reports before they go in. 导出 asks where to save the zip; the
// package is then made in the background (App.diagnostics.exportFinished).
//   ExportLogsDialog { id: exportDialog }   exportDialog.start()
Dialog {
    id: dialog

    // name -> ticked
    property var chosen: ({})
    readonly property var names: App.diagnostics.files.map(f => f.name)
                                                     .filter(name => dialog.chosen[name])

    property bool showSummary: false

    function start(): void {
        App.diagnostics.refresh();
        const all = {};
        for (const f of App.diagnostics.files)
            all[f.name] = f.suggested;
        chosen = all;
        description.text = "";
        showSummary = false;
        open();
    }

    function toggle(name: string): void {
        const next = Object.assign({}, chosen);
        next[name] = !next[name];
        chosen = next;
    }

    title: qsTr("导出诊断包")
    supportingText: qsTr("包含日志，以及系统、显卡、音频、网络（导出时会检查一次各服务器的连接）和设置的报告；不含账号、缓存和歌单内容。")
    acceptText: qsTr("导出")
    acceptEnabled: names.length > 0
    rejectText: qsTr("取消")
    width: Math.min(520, (parent ? parent.width : 520) - 48)

    onAccepted: saveDialog.open()

    // Up to a week of sessions: a list that scrolls past 360 px.
    ListView {
        id: list
        width: parent.width
        height: Math.min(contentHeight, 360)
        clip: true
        model: App.diagnostics.files
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        highlightFollowsCurrentItem: false
        Accessible.role: Accessible.List
        Accessible.name: dialog.title
        ScrollBar.vertical: ScrollBar {}
        Keys.onSpacePressed: (currentItem as ListItem)?.clicked()
        Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Space
            && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
        Keys.onReturnPressed: (currentItem as ListItem)?.clicked()
        Keys.onEnterPressed: (currentItem as ListItem)?.clicked()

        delegate: ListItem {
            id: row

            required property var modelData
            required property int index

            width: list.width
            highlighted: list.activeFocus && ListView.isCurrentItem
            text: modelData.name
            supportingText: modelData.current
                            ? qsTr("%1 · 本次运行").arg(App.format.bytes(modelData.bytes))
                            : qsTr("%1 · %2").arg(App.format.bytes(modelData.bytes))
                              .arg(App.format.relativeTime(modelData.modified))
            first: index === 0
            last: index === list.count - 1
            leading: Component {
                CheckBox {
                    checked: dialog.chosen[row.modelData.name] === true
                    focusPolicy: Qt.NoFocus
                    Accessible.name: row.modelData.name
                    onToggled: dialog.toggle(row.modelData.name)
                }
            }
            onClicked: {
                list.currentIndex = index;
                dialog.toggle(modelData.name);
            }
        }
    }

    Item {
        width: 1
        height: 16
    }
    TextArea {
        id: description
        width: parent.width
        maximumLines: 5
        placeholderText: qsTr("发生了什么、大概什么时候（可不填）")
    }
    Button {
        variant: Md.Text
        text: dialog.showSummary ? qsTr("收起报告") : qsTr("查看报告")
        onClicked: dialog.showSummary = !dialog.showSummary
    }
    SelectableText {
        width: parent.width
        visible: dialog.showSummary
        activeFocusOnTab: true
        text: dialog.showSummary ? App.diagnostics.preview(dialog.names) : ""
        font: Theme.type.bodySmall.font
        color: ColorScope.scheme.onSurfaceVariant
    }

    D.FileDialog {
        id: saveDialog
        title: qsTr("保存诊断包")
        fileMode: D.FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [qsTr("Zip 文件 (*.zip)")]
        selectedFile: App.diagnostics.packageName()
        onAccepted: App.diagnostics.exportTo(selectedFile, dialog.names, description.text)
    }
}
