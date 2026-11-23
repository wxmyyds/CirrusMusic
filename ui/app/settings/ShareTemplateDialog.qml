pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Edits the share text (pages/share.md §2): the template, chips that put a placeholder
// in at the cursor, and what it makes of a song and an artist as it is typed. 保存 keeps
// it; 恢复默认 puts the default in the field (still to be saved).
//   ShareTemplateDialog { id: shareDialog }   shareDialog.start()
Dialog {
    id: dialog

    function start(): void {
        field.text = App.settings.shareTemplate;
        open();
    }

    title: qsTr("分享文案")
    acceptText: qsTr("保存")
    rejectText: qsTr("取消")
    initialFocusItem: field
    width: Math.min(560, (parent ? parent.width : 560) - 48)
    onAccepted: App.settings.shareTemplate = field.text

    buttons: Button {
        variant: Md.Text
        enabled: field.text !== App.share.defaultTemplate
        text: qsTr("恢复默认")
        onClicked: field.text = App.share.defaultTemplate
    }

    Column {
        width: parent.width
        spacing: 16

        TextField {
            id: field
            width: parent.width
            variant: Md.Outlined
            label: qsTr("文案")
            supportingText: qsTr("[ ] 里有空的内容时，整段不显示")
        }
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: [qsTr("链接"), qsTr("名称"), qsTr("作者"), qsTr("类型")]
                delegate: Chip {
                    required property string modelData
                    text: modelData
                    glyph: Symbols.add
                    Accessible.name: qsTr("插入%1").arg(modelData)
                    onClicked: {
                        field.insert(field.cursorPosition, "{" + modelData + "}");
                        field.forceActiveFocus();
                    }
                }
            }
        }
        Column {
            width: parent.width
            spacing: 4
            Text {
                text: qsTr("预览")
                font: Theme.type.labelLarge.font
                color: ColorScope.scheme.onSurfaceVariant
            }
            Repeater {
                // A song and an artist (no author).
                model: [App.share.text(field.text, Share.Song, "186016", qsTr("晴天"), qsTr("周杰伦")),
                    App.share.text(field.text, Share.Artist, "6452", qsTr("周杰伦"), "")]
                delegate: SelectableText {
                    required property string modelData
                    width: parent.width
                    text: modelData
                    wrapMode: Text.Wrap
                    font: Theme.type.bodyMedium.font
                    color: ColorScope.scheme.onSurface
                }
            }
        }
    }
}
