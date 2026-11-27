import QtQuick
import Cirrus.Components

Item {
    id: harness
    width: 320
    height: 240
    property alias stack: pageStack
    property int destroyedPages: 0

    Component {
        id: testPage
        Item {
            property int pageId: -1
            Component.onDestruction: harness.destroyedPages += 1
        }
    }

    PageStack {
        id: pageStack
        anchors.fill: parent
        cachedPages: 2
    }

    function start(): void {
        pageStack.replaceAll(testPage, {pageId: 1});
    }
    function pushPage(id: int): void {
        pageStack.push(testPage, {pageId: id}, PageStack.Kind.Forward);
    }
    function popPage(): bool {
        return pageStack.pop();
    }
}
