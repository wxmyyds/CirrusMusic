import QtQuick
import Cirrus.Theme
import Cirrus.Presentation

// What 复制链接 copies (pages/share.md §2).
SettingsGroup {
    title: qsTr("分享")
    glyph: Symbols.share
    hue: "cyan"

    ActionSetting {
        text: qsTr("分享文案")
        // What it makes of a song.
        supportingText: App.share.text(App.settings.shareTemplate, Share.Song, "186016",
                                       qsTr("晴天"), qsTr("周杰伦"))
        actionText: qsTr("编辑")
        onTriggered: shareDialog.start()
    }

    ShareTemplateDialog {
        id: shareDialog
    }
}
