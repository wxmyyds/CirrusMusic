pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Cirrus.Theme
import Cirrus.Controls

// Phase 2 check: switches, checkboxes, radio buttons, text fields, search bar, sliders,
// chips, shortcut keys.
ControlPage {
    id: page

    SectionTitle {
        text: qsTr("开关")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 24
        Switch {
            text: qsTr("关")
        }
        Switch {
            text: qsTr("开")
            checked: true
        }
        Switch {
            text: qsTr("带图标")
            showIcons: true
        }
        Switch {
            text: qsTr("带图标")
            showIcons: true
            checked: true
        }
        Switch {
            text: qsTr("禁用")
            enabled: false
        }
        Switch {
            text: qsTr("禁用")
            enabled: false
            checked: true
        }
    }

    SectionTitle {
        text: qsTr("复选框与单选按钮")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 16
        CheckBox {
            text: qsTr("未选")
        }
        CheckBox {
            text: qsTr("已选")
            checked: true
        }
        CheckBox {
            text: qsTr("部分")
            tristate: true
            checkState: Qt.PartiallyChecked
        }
        CheckBox {
            text: qsTr("错误")
            error: true
        }
        CheckBox {
            text: qsTr("错误")
            error: true
            checked: true
        }
        CheckBox {
            text: qsTr("禁用")
            enabled: false
            checked: true
        }
    }
    Row {
        spacing: 16
        RadioButton {
            text: qsTr("标准音质")
        }
        RadioButton {
            text: qsTr("极高音质")
            checked: true
        }
        RadioButton {
            text: qsTr("无损")
        }
        RadioButton {
            text: qsTr("Hi-Res（禁用）")
            enabled: false
        }
    }

    SectionTitle {
        text: qsTr("文本框")
    }
    Caption {
        text: qsTr("标签在聚焦或有内容时上浮；左列 Filled，右列 Outlined。")
    }
    GridLayout {
        columns: 2
        columnSpacing: 24
        rowSpacing: 16
        Repeater {
            model: [Md.Filled, Md.Outlined]
            delegate: TextField {
                required property int modelData
                variant: modelData
                label: qsTr("歌单名称")
                Layout.preferredWidth: 300
            }
        }
        Repeater {
            model: [Md.Filled, Md.Outlined]
            delegate: TextField {
                required property int modelData
                variant: modelData
                label: qsTr("歌单名称")
                text: qsTr("深夜通勤")
                leadingGlyph: Symbols.queue_music
                trailingGlyph: Symbols.close
                supportingText: qsTr("最多 40 个字符")
                Layout.preferredWidth: 300
            }
        }
        Repeater {
            model: [Md.Filled, Md.Outlined]
            delegate: TextField {
                required property int modelData
                variant: modelData
                label: qsTr("手机号")
                text: "138 0000"
                error: true
                trailingGlyph: Symbols.error
                supportingText: qsTr("请输入 11 位手机号")
                Layout.preferredWidth: 300
            }
        }
        Repeater {
            model: [Md.Filled, Md.Outlined]
            delegate: TextField {
                required property int modelData
                variant: modelData
                placeholderText: qsTr("无标签，仅占位文字")
                Layout.preferredWidth: 300
            }
        }
        Repeater {
            model: [Md.Filled, Md.Outlined]
            delegate: TextField {
                required property int modelData
                variant: modelData
                label: qsTr("禁用")
                text: qsTr("不可编辑")
                enabled: false
                Layout.preferredWidth: 300
            }
        }
    }
    Caption {
        text: qsTr("多行文本框（TextArea，描边样式）：随内容变高，最多 maximumLines 行后滚动；Ctrl+Enter 发出 submitted。第二个带计数和错误。")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 16
        TextArea {
            width: 300
            placeholderText: qsTr("写下你的评论…")
        }
        TextArea {
            width: 300
            maximumLines: 3
            text: qsTr("第一行\n第二行\n第三行\n第四行，超过三行开始滚动")
            error: true
            supportingText: qsTr("太长了")
            counterText: "4100 / 4096"
        }
    }

    SectionTitle {
        text: qsTr("搜索栏")
    }
    Caption {
        text: qsTr("Esc 清空；输入内容后右侧出现清除按钮。")
    }
    RowLayout {
        spacing: 16
        SearchBar {
            placeholderText: qsTr("搜索歌曲、歌手、专辑")
        }
        SearchBar {
            placeholderText: qsTr("搜索歌曲、歌手、专辑")
            text: qsTr("落日航线")
        }
    }

    SectionTitle {
        text: qsTr("滑块")
    }
    Caption {
        text: qsTr("把手为竖条，与轨道两侧留 6 px 间隙；按下或键盘焦点时变细。第二个拖动时显示数值；第四个是进度条（roundHandle）：静止时是圆点，按下或键盘焦点时拉成竖条。")
    }
    ColumnLayout {
        spacing: 16
        Slider {
            value: 0.35
            Accessible.name: qsTr("音量")
            Layout.preferredWidth: 360
        }
        Slider {
            from: 0
            to: 100
            value: 72
            showValueLabel: true
            Accessible.name: qsTr("音量")
            Layout.preferredWidth: 360
        }
        Slider {
            value: 0.6
            trackHeight: 40
            Accessible.name: qsTr("均衡器")
            Layout.preferredWidth: 360
        }
        // A seek bar: a dot at rest that stretches into the bar while pressed.
        Slider {
            value: 0.4
            trackHeight: 8
            handleHeight: 32
            waveAmplitude: 4
            wavy: true
            roundHandle: true
            Accessible.name: qsTr("播放进度")
            Layout.preferredWidth: 360
        }
        Slider {
            value: 0
            Accessible.name: qsTr("最小值")
            Layout.preferredWidth: 360
        }
        Slider {
            value: 0.5
            enabled: false
            Accessible.name: qsTr("禁用")
            Layout.preferredWidth: 360
        }
    }

    SectionTitle {
        text: qsTr("纸片")
    }
    Caption {
        text: qsTr("建议／辅助纸片为描边样式；可选（checkable）为筛选纸片，选中时填充 SecondaryContainer 并显示对勾；可删除（removable）为输入纸片，标签后有删除按钮，长标签省略；下拉（dropdown）为带菜单的筛选纸片，尾部是下拉箭头。最后一个为禁用。下面一行是 ChipGroup：单选，一个 Tab 停留点，←／→ 移动，放不下的纸片隐藏。")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        Chip {
            text: qsTr("落日航线")
        }
        Chip {
            glyph: Symbols.history
            text: qsTr("Harbor Lights")
        }
        Chip {
            checkable: true
            checked: true
            text: qsTr("只看可播放")
        }
        Chip {
            checkable: true
            text: qsTr("华语")
        }
        Chip {
            removable: true
            text: qsTr("雾港乐队")
        }
        Chip {
            removable: true
            maximumLabelWidth: 96
            text: qsTr("一个很长很长的搜索词")
        }
        Chip {
            dropdown: true
            text: qsTr("热门")
        }
        Chip {
            dropdown: true
            checked: true
            text: "K"
        }
        Chip {
            enabled: false
            text: qsTr("不可用")
        }
    }
    // Single-select row: one Tab stop, ←/→ move, chips past the width hidden.
    ChipGroup {
        id: chipGroup
        Layout.fillWidth: true
        model: [qsTr("全部"), qsTr("华语"), qsTr("欧美"), qsTr("日本"), qsTr("韩国"), qsTr("其他")]
        currentIndex: 1
        onActivated: index => currentIndex = index
    }

    SectionTitle {
        text: qsTr("快捷键键帽")
    }
    Caption {
        text: qsTr("ShortcutKey：点击、空格或 Enter 开始录入，录入时容器变成 PrimaryContainer，按下的键交给 recorded，Esc 取消，Backspace 清除，Tab 或点别处也取消。只有图标时是描边的“添加”按钮。第三个为录入中，最后两个为禁用。")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        ShortcutKey {
            text: "Ctrl+P"
            onRecorded: recording = false
        }
        ShortcutKey {
            text: "Ctrl+Shift+←"
            onRecorded: recording = false
        }
        ShortcutKey {
            text: "F5"
            recording: true
            onRecorded: recording = false
        }
        ShortcutKey {
            glyph: Symbols.add
            hint: qsTr("添加按键")
            onRecorded: recording = false
        }
        ShortcutKey {
            enabled: false
            text: "Ctrl+,"
        }
        ShortcutKey {
            enabled: false
            glyph: Symbols.add
            hint: qsTr("添加按键")
        }
    }
}
