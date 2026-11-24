import QtQuick
import Cirrus.Theme
import Cirrus.Presentation
import Cirrus.Controls

// Playback. The output device comes with the backend (docs/ui/pages/settings.md §1).
SettingsGroup {
    title: qsTr("播放")
    glyph: Symbols.graphic_eq
    hue: "pink"

    SwitchSetting {
        text: qsTr("允许 Bilibili 音源")
        supportingText: qsTr("仅网易云明确受限或仅提供试听时使用；失败不切换音源\n字幕缺失时使用网易云歌词")
        value: App.settings.biliFallback
        onSwitched: value => App.settings.biliFallback = value
    }

    MenuSetting {
        text: qsTr("音质")
        supportingText: qsTr("在线播放的音质；没有这一档时用最接近的")
        // The backend's AudioQuality.
        options: [{
                value: 0,
                text: qsTr("标准"),
                description: qsTr("128 kbps")
            }, {
                value: 1,
                text: qsTr("极高"),
                description: qsTr("320 kbps")
            }, {
                value: 2,
                text: qsTr("无损"),
                description: qsTr("FLAC，通常需要会员")
            }, {
                value: 3,
                text: qsTr("Hi-Res"),
                description: qsTr("高解析度 FLAC，通常需要会员")
            }]
        value: App.settings.audioQuality
        onChosen: value => App.settings.audioQuality = value
    }
    ChoiceSetting {
        text: qsTr("歌曲衔接")
        supportingText: qsTr("一首歌怎样接到下一首")
        // The backend's TransitionMode.
        options: [{
                value: 0,
                text: qsTr("智能混音")
            }, {
                value: 1,
                text: qsTr("淡入淡出")
            }, {
                value: 2,
                text: qsTr("无缝")
            }]
        value: App.settings.trackTransition
        onChosen: value => App.settings.trackTransition = value
    }
    SliderSetting {
        visible: App.settings.trackTransition === 1
        text: qsTr("淡入淡出时长")
        from: 1
        to: 12
        stepSize: 1
        format: value => qsTr("%1 秒").arg(Math.round(value))
        value: App.settings.crossfadeSeconds
        onMoved: value => App.settings.crossfadeSeconds = value
    }
    SwitchSetting {
        text: qsTr("音量均衡")
        supportingText: qsTr("每首歌听起来差不多响")
        value: App.settings.normalizeVolume
        onSwitched: value => App.settings.normalizeVolume = value
    }
    SwitchSetting {
        text: qsTr("启动时继续播放")
        supportingText: qsTr("打开应用后接着播上次的歌")
        value: App.settings.resumeOnStart
        onSwitched: value => App.settings.resumeOnStart = value
    }
}
