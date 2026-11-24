import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// On a player's cover while listening together (docs/ui/pages/together.md §4): a Primary
// disc with the group symbol, Error while the room reconnects; hovering says with whom.
// Clicks pass through to the cover.
Rectangle {
    id: badge

    property real size: 20

    visible: App.together.active
    width: size
    height: size
    radius: size / 2
    color: App.together.connected ? ColorScope.scheme.primary : ColorScope.scheme.error
    border.width: 2
    border.color: ColorScope.scheme.surfaceContainer

    Icon {
        anchors.centerIn: parent
        glyph: App.together.connected ? Symbols.group : Symbols.sync_problem
        size: Math.round(badge.size * 0.6)
        fill: 1
        color: App.together.connected ? ColorScope.scheme.onPrimary : ColorScope.scheme.onError
    }
    HoverHandler {
        id: hover
    }
    ToolTip {
        parent: badge
        visible: hover.hovered
        text: !App.together.connected ? qsTr("一起听 · 正在重新连接")
              : App.together.partnerName !== "" ? qsTr("一起听 · 你和 %1").arg(App.together.partnerName)
              : qsTr("一起听 · 等好友加入")
    }
}
