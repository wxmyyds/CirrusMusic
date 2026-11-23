pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Controls
import Cirrus.Presentation

// Which of the floating bar's buttons show (App.settings.miniPlayerHidden), each a
// checkable item; the bar's right-click and the settings row open it. The bar still drops
// buttons it has no room for.
//   MiniPlayerButtonsMenu { id: buttonsMenu }   buttonsMenu.popup()
Menu {
    id: menu

    // [{ id, text }] in the bar's order.
    readonly property var buttons: [{
            id: "like",
            text: qsTr("喜欢")
        }, {
            id: "mode",
            text: qsTr("播放顺序")
        }, {
            id: "volume",
            text: qsTr("音量")
        }, {
            id: "queue",
            text: qsTr("播放队列")
        }, {
            id: "comments",
            text: qsTr("评论")
        }]

    // The shown ones' names, "、"-joined (the settings row says it); 无 when none.
    function shownText(): string {
        const hidden = App.settings.miniPlayerHidden;
        const shown = buttons.filter(button => !hidden.includes(button.id))
                             .map(button => button.text);
        return shown.length > 0 ? shown.join(qsTr("、")) : qsTr("无");
    }

    function toggle(id: string): void {
        const hidden = Array.from(App.settings.miniPlayerHidden);
        const at = hidden.indexOf(id);
        if (at >= 0)
            hidden.splice(at, 1);
        else
            hidden.push(id);
        App.settings.miniPlayerHidden = hidden;
    }

    // A title, so the items read as buttons to show, not as actions (喜欢).
    MenuItem {
        enabled: false
        text: qsTr("显示的按钮")
    }
    Instantiator {
        model: menu.buttons
        delegate: MenuItem {
            required property var modelData
            checkable: true
            checked: !App.settings.miniPlayerHidden.includes(modelData.id)
            text: modelData.text
            onTriggered: {
                menu.toggle(modelData.id);
                // Back to following the setting (a click set it by hand).
                checked = Qt.binding(() => !App.settings.miniPlayerHidden.includes(modelData.id));
            }
        }
        onObjectAdded: (index, object) => menu.insertItem(index + 1, object as MenuItem)
        onObjectRemoved: (index, object) => menu.removeItem(object as MenuItem)
    }
}
