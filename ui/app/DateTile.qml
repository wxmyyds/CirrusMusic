import QtQuick
import Cirrus.Theme

// A day as a cover (the daily songs page, album-daily.md §2): the date as a large
// thin numeral, like the home page's "today" card, with the month and weekday under
// it. Small tiles (the compact bar) show the numeral only.
Rectangle {
    id: tile

    property date day: new Date()
    readonly property bool small: width < 100

    radius: small ? Theme.shape.small : Theme.shape.extraLarge
    color: Theme.color.primaryContainer
    Accessible.name: Qt.locale().toString(day, Locale.LongFormat)

    Text {
        readonly property real size: Math.round(tile.width * (tile.small ? 0.6 : 0.5))
        x: tile.small ? (tile.width - width) / 2 : Math.round(tile.width * 0.12)
        y: tile.small ? (tile.height - height) / 2 : Math.round(tile.width * 0.1)
        text: Qt.formatDate(tile.day, "d")
        color: Theme.color.onPrimaryContainer
        font: Fonts.font(tile.small ? 500 : 200, size)
        Accessible.ignored: true
    }
    Text {
        x: Math.round(tile.width * 0.12)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(tile.width * 0.1)
        width: tile.width - 2 * x
        visible: !tile.small
        text: qsTr("%1月 · %2").arg(tile.day.getMonth() + 1)
                               .arg(Qt.locale().toString(tile.day, "dddd"))
        elide: Text.ElideRight
        color: Theme.color.onPrimaryContainer
        font: tile.width >= 180 ? Theme.type.titleMedium.font : Theme.type.titleSmall.font
        Accessible.ignored: true
    }
}
