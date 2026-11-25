import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// What the newer release brings and which file to get for this copy; the release page
// opens in the browser (docs/tasks/update-check.md §4, §6). The Windows installed copy
// downloads the installer instead (§9): the about settings and a snackbar follow it.
Dialog {
    id: dialog

    readonly property int install: App.updates.install
    readonly property string version: App.updates.latestVersion

    reading: true
    glyph: Symbols.download
    title: App.updates.title
    supportingText: App.updates.notes
    acceptText: App.updates.canInstall
                ? (App.updates.download === UpdateCheck.Downloaded ? qsTr("安装并重启")
                   : App.updates.download === UpdateCheck.Downloading ? qsTr("正在下载…")
                   : qsTr("下载并安装"))
                : install === UpdateCheck.Aur ? qsTr("查看发布页") : qsTr("前往下载")
    acceptEnabled: App.updates.download !== UpdateCheck.Downloading
    rejectText: qsTr("稍后")
    onAccepted: {
        if (!App.updates.canInstall)
            App.updates.openRelease();
        else if (App.updates.download === UpdateCheck.Downloaded)
            App.updates.installUpdate();
        else
            App.updates.downloadInstaller();
    }

    SelectableText {
        width: parent.width
        text: {
            const published = App.format.date(App.updates.publishedAt);
            const hint = dialog.hint();
            return published !== "" ? qsTr("%1发布 · %2").arg(published).arg(hint) : hint;
        }
        color: ColorScope.scheme.onSurfaceVariant
        font: Theme.type.bodyMedium.font
    }

    function hint(): string {
        switch (install) {
        case UpdateCheck.Deb:
            return qsTr("下载 cirrus_%1_amd64.deb 安装").arg(version);
        case UpdateCheck.AppImage:
            return qsTr("下载 Cirrus-%1-x86_64.AppImage，替换现在的文件").arg(version);
        case UpdateCheck.WindowsSetup:
            return App.updates.canInstall ? qsTr("下载完成后安装并重启 Cirrus，设置和数据会保留")
                                          : qsTr("下载 Cirrus-%1-windows-x64-setup.exe 运行，设置和数据会保留").arg(version);
        case UpdateCheck.WindowsZip:
            return qsTr("下载 Cirrus-%1-windows-x64.zip，解压后替换现在的文件夹").arg(version);
        case UpdateCheck.MacDmg:
            return qsTr("下载 Cirrus-%1-macos-arm64.dmg，把 Cirrus 拖进“应用程序”替换旧版").arg(version);
        case UpdateCheck.Aur:
            return qsTr("通过 AUR 更新，例如 paru -Syu");
        default:
            return qsTr("在发布页下载");
        }
    }
}
