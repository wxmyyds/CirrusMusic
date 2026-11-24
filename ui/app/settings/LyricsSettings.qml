import QtQuick
import Cirrus.Theme
import Cirrus.Presentation

// What the lyrics show besides the original lines.
SettingsGroup {
    title: qsTr("歌词")
    glyph: Symbols.lyrics
    hue: "pink"

    SwitchSetting {
        text: qsTr("向外部提供歌词 (私货)")
        supportingText: qsTr("默认关闭；开启后向当前用户会话中的应用提供原文、翻译、罗马音和逐行／逐字时序\n关闭时仅保留标准 MPRIS 歌曲信息，由其他歌词软件自行搜索")
        value: App.settings.externalLyrics
        onSwitched: value => App.settings.externalLyrics = value
    }
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
