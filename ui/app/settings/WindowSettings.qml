import QtQuick
import Cirrus.Theme
import Cirrus.Window
import Cirrus.Presentation
import CirrusMusic

// The window and how the app lives on the desktop.
SettingsGroup {
    id: group

    title: qsTr("窗口与系统")
    glyph: Symbols.desktop_windows
    hue: "grey"

    ChoiceSetting {
        text: qsTr("窗口装饰")
        supportingText: qsTr("窗口按钮、阴影和圆角由谁绘制")
        options: [
            {
                value: TitleBarMode.Custom,
                text: qsTr("应用绘制")
            },
            {
                value: TitleBarMode.Hidden,
                text: qsTr("无")
            },
            {
                value: TitleBarMode.System,
                text: qsTr("系统")
            }
        ]
        value: (group.Window.window as AppWindow)?.titleBar ?? -1
        onChosen: value => {
            App.settings.titleBar = value;
            (group.Window.window as AppWindow)?.setTitleBar(value);
        }
    }
    ChoiceSetting {
        text: qsTr("关闭主窗口时")
        options: [
            {
                value: -1,
                text: qsTr("每次询问")
            },
            {
                value: 1,
                text: Desktop.trayAvailable ? qsTr("最小化到托盘") : qsTr("后台播放")
            },
            {
                value: 0,
                text: qsTr("退出")
            }
        ]
        value: App.settings.closeAction
        onChosen: value => App.settings.closeAction = value
    }
    SliderSetting {
        text: qsTr("滚轮滚动速度")
        supportingText: qsTr("鼠标滚轮每一格滚动的距离")
        from: 0.5
        to: 3
        stepSize: 0.1
        format: value => Math.round(value * 100) + "%"
        value: App.settings.wheelSpeed
        onMoved: value => App.settings.wheelSpeed = value
    }
    SwitchSetting {
        visible: Desktop.switchableGraphics
        text: qsTr("使用独立显卡")
        supportingText: qsTr("用独立显卡绘制界面，重启后生效")
        value: App.settings.preferDiscreteGpu
        onSwitched: value => App.settings.preferDiscreteGpu = value
    }
    SwitchSetting {
        // The local now-playing interface (tools/now-playing/README.md) is Linux only.
        visible: Qt.platform.os === "linux"
        text: qsTr("向其他程序提供播放状态 (私货)")
        supportingText: qsTr("歌名、进度和歌词，供桌面歌词、状态栏等读取")
        value: App.settings.shareNowPlaying
        onSwitched: value => App.settings.shareNowPlaying = value
    }
    SwitchSetting {
        visible: App.updates.canCheckAutomatically
        text: qsTr("自动检查更新")
        supportingText: qsTr("每天向 GitHub 查询一次新版本")
        value: App.settings.updatesAuto
        onSwitched: value => App.settings.updatesAuto = value
    }
}
