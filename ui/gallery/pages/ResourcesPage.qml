pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Samples

// Text uses the selected system/custom family; Material Symbols supplies bundled icons.
// This page previews the text weights, icon glyphs and sample covers.
B.ScrollView {
    id: page

    ScrollBar.vertical: ScrollBar {
        parent: page
        x: page.width - width
        height: page.availableHeight
    }

    required property color foreground
    required property color foregroundMuted

    contentWidth: availableWidth

    component SectionTitle: B.Label {
        color: page.foreground
        font.pixelSize: 22
        font.weight: Font.DemiBold
        Layout.topMargin: 24
    }

    component Caption: B.Label {
        color: page.foregroundMuted
        font.pixelSize: 13
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    component Sample: Text {
        property int weight: 400
        property real size: 22
        color: page.foreground
        font: Fonts.font(weight, size)
        elide: Text.ElideRight
        Layout.fillWidth: true
    }

    ColumnLayout {
        width: page.availableWidth - 48
        x: 24
        spacing: 8

        SectionTitle {
            text: qsTr("字体")
        }
        Caption {
            text: qsTr("正文：%1 · 图标：%2。正文使用系统字体，也可以在设置中选择已安装或导入的字体。")
                .arg(Fonts.sans).arg(Fonts.symbols || qsTr("未加载"))
        }

        Caption {
            text: qsTr("字重由系统字体引擎处理。")
            Layout.topMargin: 12
        }
        Repeater {
            model: [100, 200, 300, 400, 500, 600, 700, 800, 900]
            delegate: Sample {
                required property int modelData
                text: modelData + "  Harbor Lights 落日航线 0123456789"
                weight: modelData
            }
        }

        Caption {
            text: qsTr("混排与回退：中文、日文、希腊、西里尔、emoji")
            Layout.topMargin: 12
        }
        Sample {
            text: "Cirrus 云端音乐 · 雾港乐队《落日航线》— きいろい夢 · Ωμέγα · Привет · 🎵"
        }
        Sample {
            text: "播放中：Last Train Home — Night Transit, Prism Theory (2025)"
            size: 15
        }

        SectionTitle {
            text: qsTr("图标（%1 个）").arg(Symbols.names.length)
        }
        RowLayout {
            spacing: 16
            B.Label {
                text: qsTr("填充")
                color: page.foreground
            }
            Switch {
                id: fillSwitch
            }
            B.Label {
                text: qsTr("字重 %1").arg(weightSlider.value)
                color: page.foreground
            }
            Slider {
                id: weightSlider
                from: 100
                to: 700
                stepSize: 100
                value: 400
            }
            B.Label {
                text: qsTr("尺寸 %1").arg(sizeSlider.value)
                color: page.foreground
            }
            Slider {
                id: sizeSlider
                from: 20
                to: 48
                stepSize: 4
                value: 24
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: Symbols.names
                delegate: Column {
                    id: iconCell
                    required property string modelData
                    width: 96
                    spacing: 2
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        glyph: Symbols[iconCell.modelData]
                        size: sizeSlider.value
                        fill: fillSwitch.checked ? 1 : 0
                        weight: weightSlider.value
                        color: page.foreground
                    }
                    Text {
                        width: iconCell.width
                        text: iconCell.modelData
                        color: page.foregroundMuted
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                    }
                }
            }
        }

        SectionTitle {
            text: qsTr("示例封面（%1 张）").arg(Samples.covers.length)
        }
        Flow {
            Layout.fillWidth: true
            Layout.bottomMargin: 24
            spacing: 12
            Repeater {
                model: Samples.covers
                delegate: Column {
                    id: coverCell
                    required property string modelData
                    spacing: 4
                    Image {
                        width: 128
                        height: 128
                        source: Samples.cover(coverCell.modelData)
                        sourceSize: Qt.size(256, 256)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    Text {
                        text: coverCell.modelData
                        color: page.foregroundMuted
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
