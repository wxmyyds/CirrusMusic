import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// What the newer release brings and which file to get for this copy. The matching
// asset, or the GitHub release page when none matches, opens in the browser. The
// Windows installed copy downloads the installer instead: the about settings and
// a snackbar follow it.
Dialog {
    id: dialog

    readonly property string assetName: App.updates.assetName

    reading: true
    glyph: Symbols.download
    title: App.updates.title
    supportingText: App.updates.notes
    acceptText: App.updates.canInstall
                ? (App.updates.download === UpdateCheck.Downloaded ? qsTr("安装并重启")
                   : App.updates.download === UpdateCheck.Downloading ? qsTr("正在下载…")
                   : qsTr("下载并安装"))
                : assetName === "" ? qsTr("查看 GitHub 发布页") : qsTr("前往下载")
    acceptEnabled: App.updates.download !== UpdateCheck.Downloading
    rejectText: qsTr("稍后")
    onAccepted: {
        if (!App.updates.canInstall)
            App.updates.openDownload();
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
        if (assetName === "")
            return qsTr("在 GitHub 发布页查看可用下载");
        return App.updates.canInstall
                ? qsTr("下载 %1，完成后安装并重启 Cirrus，设置和数据会保留").arg(assetName)
                : qsTr("下载 %1").arg(assetName);
    }
}
