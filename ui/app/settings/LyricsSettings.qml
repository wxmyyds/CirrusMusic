import QtQuick
import Cirrus.Theme
import Cirrus.Presentation

// What the lyrics show besides the original lines.
SettingsGroup {
    title: qsTr("歌词")
    glyph: Symbols.lyrics
    hue: "pink"

    SwitchSetting {
        text: qsTr("显示翻译")
        supportingText: qsTr("在原文下方显示翻译")
        value: App.settings.showTranslation
        onSwitched: value => App.settings.showTranslation = value
    }
    SwitchSetting {
        text: qsTr("显示音译")
        supportingText: qsTr("在原文下方显示罗马音")
        value: App.settings.showRomanization
        onSwitched: value => App.settings.showRomanization = value
    }
}
