import QtQuick
import Cirrus.Theme
import Cirrus.Presentation

// How the player page looks (docs/ui/pages/player.md §9.2). Shared by the settings
// page and the player's display settings panel (PlayerPanelSettings), where it takes
// the cover's colors.
SettingsGroup {
    title: qsTr("播放页")
    glyph: Symbols.music_note
    hue: "pink"

    ChoiceSetting {
        text: qsTr("风格")
        // 0 the default (Apple Music-like), 1 Material (docs/ui/pages/player-expressive.md).
        options: [{
                value: 0,
                text: qsTr("默认")
            }, {
                value: 1,
                text: qsTr("Material")
            }]
        value: App.settings.playerStyle
        onChosen: value => App.settings.playerStyle = value
    }
    ChoiceSetting {
        // The Material style's and the mini and side players' colors from the cover.
        text: qsTr("取色")
        options: [{
                value: 0,
                text: qsTr("标准")
            }, {
                value: 1,
                text: qsTr("鲜明")
            }, {
                value: 2,
                text: qsTr("表现力")
            }, {
                value: 3,
                text: qsTr("贴近封面")
            }]
        value: App.settings.playerColorStyle
        onChosen: value => App.settings.playerColorStyle = value
    }
    ChoiceSetting {
        text: qsTr("明暗")
        options: [{
                value: 0,
                text: qsTr("跟随外观")
            }, {
                value: 1,
                text: qsTr("深色")
            }, {
                value: 2,
                text: qsTr("浅色")
            }]
        value: App.settings.playerTheme
        onChosen: value => App.settings.playerTheme = value
    }
    ChoiceSetting {
        // The Material style has a solid background of its own; these rows are the
        // default style's.
        shown: App.settings.playerStyle === 0
        text: qsTr("背景")
        // Stored values: 0 flowing light, 3 still light, 1 blurred cover, 2 solid.
        options: [{
                value: 0,
                text: qsTr("流光")
            }, {
                value: 3,
                text: qsTr("静态")
            }, {
                value: 1,
                text: qsTr("模糊封面")
            }, {
                value: 2,
                text: qsTr("纯色")
            }]
        value: App.settings.playerBackground
        onChosen: value => App.settings.playerBackground = value
    }
    ChoiceSetting {
        shown: App.settings.playerStyle === 0 && App.settings.playerBackground === 0
        text: qsTr("流动速度")
        options: [{
                value: 0,
                text: qsTr("慢")
            }, {
                value: 1,
                text: qsTr("标准")
            }, {
                value: 2,
                text: qsTr("快")
            }]
        value: App.settings.playerFlowSpeed
        onChosen: value => App.settings.playerFlowSpeed = value
    }
    SwitchSetting {
        shown: App.settings.playerStyle === 0
        text: qsTr("封面立体倾斜")
        value: App.settings.playerTilt
        onSwitched: value => App.settings.playerTilt = value
    }
    ChoiceSetting {
        shown: App.settings.playerStyle === 0
        text: qsTr("控制区位置")
        options: [{
                value: 0,
                text: qsTr("封面下方")
            }, {
                value: 1,
                text: qsTr("底部")
            }]
        value: App.settings.playerControls
        onChosen: value => App.settings.playerControls = value
    }
    ChoiceSetting {
        shown: App.settings.playerStyle === 0
        text: qsTr("歌名位置")
        options: [{
                value: 0,
                text: qsTr("封面下方")
            }, {
                value: 1,
                text: qsTr("底部")
            }]
        value: App.settings.playerHeader
        onChosen: value => App.settings.playerHeader = value
    }
    ChoiceSetting {
        text: qsTr("右侧时间")
        options: [{
                value: 0,
                text: qsTr("剩余时间")
            }, {
                value: 1,
                text: qsTr("总时长")
            }]
        value: App.settings.playerTimeRight
        onChosen: value => App.settings.playerTimeRight = value
    }
    MenuSetting {
        shown: App.settings.playerStyle === 0
        text: qsTr("自动隐藏控件")
        options: [{
                value: 0,
                text: qsTr("关")
            }, {
                value: 3,
                text: qsTr("3 秒")
            }, {
                value: 5,
                text: qsTr("5 秒")
            }, {
                value: 10,
                text: qsTr("10 秒")
            }]
        value: App.settings.playerAutoHide
        onChosen: value => App.settings.playerAutoHide = value
    }
    SliderSetting {
        text: qsTr("歌词字号")
        from: 16
        to: 96
        stepSize: 1
        value: App.settings.lyricSize
        onMoved: value => App.settings.lyricSize = value
    }
    SliderSetting {
        text: qsTr("歌词行距")
        from: 2
        to: 30
        stepSize: 1
        format: value => (value / 10).toFixed(1)
        value: App.settings.lyricSpacing
        onMoved: value => App.settings.lyricSpacing = value
    }
    SliderSetting {
        text: qsTr("歌词字重")
        from: 100
        to: 900
        stepSize: 50
        value: App.settings.lyricWeight
        onMoved: value => App.settings.lyricWeight = value
    }
    ChoiceSetting {
        text: qsTr("歌词字体")
        options: [{
                value: 0,
                text: qsTr("跟随界面")
            }, {
                value: 2,
                text: qsTr("自定义")
            }]
        value: App.settings.lyricFontSource
        onChosen: value => App.settings.lyricFontSource = value
    }
    ActionSetting {
        shown: App.settings.lyricFontSource === 2
        text: qsTr("自定义字体")
        supportingText: App.settings.lyricFontFamily !== "" && Fonts.families().includes(
                            App.settings.lyricFontFamily) ? App.settings.lyricFontFamily : qsTr("未选择")
        actionText: qsTr("选择…")
        onTriggered: lyricFontPicker.open()
    }
    ChoiceSetting {
        text: qsTr("歌词对齐")
        options: [{
                value: 0,
                text: qsTr("左对齐")
            }, {
                value: 1,
                text: qsTr("居中")
            }, {
                value: 2,
                text: qsTr("靠右")
            }]
        value: App.settings.lyricAlign
        onChosen: value => App.settings.lyricAlign = value
    }
    ChoiceSetting {
        text: qsTr("当前行位置")
        options: [{
                value: 0,
                text: qsTr("偏上")
            }, {
                value: 1,
                text: qsTr("居中")
            }]
        value: App.settings.lyricAnchor
        onChosen: value => App.settings.lyricAnchor = value
    }
    SwitchSetting {
        shown: App.settings.showRomanization
        text: qsTr("日语音译注音式")
        supportingText: qsTr("罗马音标在字的上方")
        value: App.settings.lyricRuby
        onSwitched: value => App.settings.lyricRuby = value
    }
    SwitchSetting {
        text: qsTr("逐字高亮")
        value: App.settings.lyricWordFill
        onSwitched: value => App.settings.lyricWordFill = value
    }
    SwitchSetting {
        text: qsTr("模糊其他行")
        value: App.settings.lyricBlur
        onSwitched: value => App.settings.lyricBlur = value
    }
    SwitchSetting {
        text: qsTr("长音光晕")
        value: App.settings.lyricGlow
        onSwitched: value => App.settings.lyricGlow = value
    }

    FontPickerDialog {
        id: lyricFontPicker
        current: App.settings.lyricFontFamily
        onPicked: family => App.settings.lyricFontFamily = family
    }
}
