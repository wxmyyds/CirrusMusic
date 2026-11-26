pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The app, its version, and what it is built with.
SettingsGroup {
    id: group

    // The open source parts and fonts, with their licenses (third_party/; the full list
    // with texts: packaging/THIRD-PARTY-NOTICES.md).
    readonly property var components: [{
            name: "Qt",
            license: "LGPL-3.0",
            use: qsTr("应用框架")
        }, {
            name: "FFmpeg",
            license: "LGPL-2.1",
            use: qsTr("音频解码")
        }, {
            name: "QtKeychain",
            license: "BSD-3-Clause",
            use: qsTr("保存登录状态")
        }, {
            name: "OpenSSL",
            license: "Apache-2.0",
            use: qsTr("加密")
        }, {
            name: "zlib",
            license: "Zlib",
            use: qsTr("解压缩")
        }, {
            name: "QR Code generator",
            license: "MIT",
            use: qsTr("登录二维码")
        }, {
            name: "Material Symbols",
            license: "Apache-2.0",
            use: qsTr("图标")
        }, {
            name: "material-color-utilities",
            license: "Apache-2.0",
            use: qsTr("配色生成")
        }, {
            name: "m3shapes",
            license: "Apache-2.0",
            use: qsTr("形状与变形")
        }, {
            name: "NeteaseCloudMusicApi Enhanced",
            license: "MIT",
            use: qsTr("接口参考")
        }]

    title: qsTr("关于")
    glyph: Symbols.info
    hue: "grey"

    SettingRow {
        text: qsTr("Cirrus %1").arg(Qt.application.version)
        supportingText: qsTr("非官方客户端，与网易云音乐没有关联。以 MIT 许可开源。界面文字使用系统字体。")
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
        leading: Component {
            Image {
                width: 40
                height: 40
                source: "qrc:/qt/qml/CirrusMusic/icons/cirrus-128.png"
                smooth: true
                mipmap: true
            }
        }
    }
    // GitHub's latest release (docs/tasks/update-check.md §4).
    ActionSetting {
        readonly property int status: App.updates.status
        readonly property int download: App.updates.canInstall ? App.updates.download
                                                               : UpdateCheck.NoDownload

        text: qsTr("更新")
        supportingText: {
            // The installer's download (Windows, update-check.md §9) comes first.
            switch (download) {
            case UpdateCheck.Downloading:
                return qsTr("正在下载 %1 · %2%").arg(App.updates.latestVersion)
                        .arg(Math.floor(App.updates.downloadProgress * 100));
            case UpdateCheck.Downloaded:
                return qsTr("%1 已下载").arg(App.updates.latestVersion);
            case UpdateCheck.DownloadFailed:
                return qsTr("下载失败，可以重试或到发布页下载");
            }
            switch (status) {
            case UpdateCheck.Checking:
                return qsTr("正在检查…");
            case UpdateCheck.Available:
                return qsTr("新版本 %1").arg(App.updates.latestVersion);
            case UpdateCheck.UpToDate:
                return qsTr("已是最新版本");
            case UpdateCheck.Failed:
                switch (App.updates.failure) {
                case UpdateCheck.Limited:
                    return qsTr("GitHub 暂时拒绝了请求，请稍后再试");
                case UpdateCheck.Invalid:
                    return qsTr("检查失败：GitHub 的回复无法识别");
                default:
                    return qsTr("检查失败：网络不可用");
                }
            default:
                return App.updates.lastCheck > 0
                        ? qsTr("上次检查：%1").arg(App.format.relativeTime(App.updates.lastCheck)) : "";
            }
        }
        actionText: download === UpdateCheck.Downloading ? qsTr("取消")
                    : download === UpdateCheck.Downloaded ? qsTr("安装并重启")
                    : status === UpdateCheck.Available ? qsTr("查看") : qsTr("检查更新")
        actionEnabled: status !== UpdateCheck.Checking || download !== UpdateCheck.NoDownload
        onTriggered: {
            if (download === UpdateCheck.Downloading)
                App.updates.cancelDownload();
            else if (download === UpdateCheck.Downloaded)
                App.updates.installUpdate();
            else if (status === UpdateCheck.Available)
                updateDialog.open();
            else
                App.updates.check();
        }
    }
    ActionSetting {
        text: qsTr("第三方许可")
        supportingText: ""
        actionText: qsTr("查看")
        onTriggered: licensesDialog.open()
    }

    UpdateDialog {
        id: updateDialog
    }
    Dialog {
        id: licensesDialog
        title: qsTr("第三方许可")
        acceptText: qsTr("关闭")
        Column {
            width: 400
            spacing: 12
            Repeater {
                model: group.components
                delegate: Column {
                    required property var modelData
                    width: parent.width
                    Text {
                        width: parent.width
                        text: parent.modelData.name
                        color: ColorScope.scheme.onSurface
                        font: Theme.type.titleSmall.font
                    }
                    Text {
                        width: parent.width
                        text: qsTr("%1 · %2").arg(parent.modelData.use).arg(parent.modelData.license)
                        wrapMode: Text.Wrap
                        color: ColorScope.scheme.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }
                }
            }
        }
    }
}
