pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Presentation
import Cirrus.Controls
import Cirrus.Components

// Light or dark, the colors, the text font, motion.
SettingsGroup {
    title: qsTr("外观")
    glyph: Symbols.palette
    hue: "orange"

    ChoiceSetting {
        text: qsTr("明暗")
        options: [{
                value: 0,
                text: qsTr("跟随系统")
            }, {
                value: 1,
                text: qsTr("亮色")
            }, {
                value: 2,
                text: qsTr("暗色")
            }]
        value: App.settings.themeMode
        onChosen: value => App.settings.themeMode = value
    }
    // Theme.SeedSource: Cover (0), Fixed (1), System (2).
    ChoiceSetting {
        text: qsTr("主题色来源")
        supportingText: qsTr("界面配色从哪个颜色生成")
        options: [{
                value: 2,
                text: qsTr("系统强调色")
            }, {
                value: 0,
                text: qsTr("跟随封面")
            }, {
                value: 1,
                text: qsTr("固定")
            }]
        value: App.settings.seedSource
        onChosen: value => App.settings.seedSource = value
    }
    SettingRow {
        shown: App.settings.seedSource === 1
        text: qsTr("主题色")
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
        trailing: Component {
            SeedSwatches {
                value: App.settings.fixedSeed
                onChosen: value => App.settings.fixedSeed = value
                onCustomRequested: customSeed.start()
            }
        }
    }
    MenuSetting {
        text: qsTr("配色风格")
        supportingText: qsTr("由主题色生成配色的方式")
        // Theme.Variant; the others (Content, Fidelity, Rainbow, FruitSalad) suit
        // colors taken from pictures, not a setting (settings.md §2).
        options: [{
                value: 0,
                text: qsTr("标准"),
                description: qsTr("柔和")
            }, {
                value: 1,
                text: qsTr("表现力"),
                description: qsTr("色相更丰富")
            }, {
                value: 2,
                text: qsTr("鲜明"),
                description: qsTr("饱和度更高")
            }, {
                value: 5,
                text: qsTr("中性"),
                description: qsTr("接近灰色")
            }, {
                value: 6,
                text: qsTr("单色"),
                description: qsTr("只有灰阶")
            }]
        value: App.settings.variant
        onChosen: value => App.settings.variant = value
    }
    ChoiceSetting {
        text: qsTr("字体")
        supportingText: qsTr("界面文字的字体")
        // No bundled choice in builds without MiSans.
        options: [{
                value: Fonts.Bundled,
                text: qsTr("内置（MiSans）")
            }, {
                value: Fonts.System,
                text: qsTr("系统字体")
            }, {
                value: Fonts.Custom,
                text: qsTr("自定义")
            }].filter(option => option.value !== Fonts.Bundled || Fonts.bundledAvailable)
        value: App.settings.fontSource
        onChosen: value => App.settings.fontSource = value
    }
    ActionSetting {
        shown: App.settings.fontSource === Fonts.Custom
        text: qsTr("自定义字体")
        supportingText: Fonts.customFamily !== "" && Fonts.sans === Fonts.customFamily
            ? Fonts.customFamily : qsTr("未选择")
        actionText: qsTr("选择…")
        onTriggered: fontPicker.open()
    }
    SwitchSetting {
        text: qsTr("水波进度条")
        supportingText: qsTr("迷你播放条播放时的进度条")
        value: App.settings.miniWave
        onSwitched: value => App.settings.miniWave = value
    }
    // Which of the floating bar's buttons show; also on its right-click.
    SettingRow {
        id: barButtons
        function openMenu(): void {
            const button = barButtons.trailingItem as Item;
            if (button)
                barButtonsMenu.popup(button, 0, button.height + 4);
        }
        text: qsTr("播放条按钮")
        supportingText: barButtonsMenu.shownText()
        onClicked: openMenu()
        Accessible.description: supportingText
        trailing: Component {
            Button {
                focusPolicy: Qt.NoFocus
                variant: Md.Outlined
                size: Md.Small
                trailingGlyph: Symbols.keyboard_arrow_down
                text: qsTr("选择")
                onClicked: barButtons.openMenu()
            }
        }
        MiniPlayerButtonsMenu {
            id: barButtonsMenu
        }
    }
    SwitchSetting {
        text: qsTr("减少动态效果")
        supportingText: qsTr("用淡入淡出代替位移和缩放")
        value: App.settings.reducedMotion
        onSwitched: value => App.settings.reducedMotion = value
    }

    CustomSeedDialog {
        id: customSeed
    }

    FontPickerDialog {
        id: fontPicker
        current: App.settings.customFontFamily
        onPicked: family => App.settings.customFontFamily = family
    }
}
