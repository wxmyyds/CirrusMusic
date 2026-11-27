pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// Page history of the shell's content area with the MD3 transitions of
// docs/ui/motion-choreography.md §3:
// - push(): forward, shared X axis (old page 30 left and out, new one in from 30 right)
// - pop(): back, the same mirrored
// - replaceAll(): new top-level destination, history cleared; fade through with the new
//   page scaling up from 0.92
// One progress spring drives each transition; popping during a push reverses that same
// transition, and a new navigation mid-way starts from what is on screen. The new page
// takes input at once. The newest `cachedPages` pages stay alive (keeping scroll
// position); older ones are destroyed and recreated when navigated back to, with
// `saveState()` / `restoreState(state)` if the page defines them.
//   PageStack { id: stack; Component.onCompleted: replaceAll(homePage) }
//   stack.push(playlistPage, { playlistKey: key })
Item {
    id: root

    property int cachedPages: 4
    readonly property int depth: entries.length
    readonly property Item currentItem: entries.length > 0 ? entries[entries.length - 1].item : null

    // {component, properties, item, state}; the last one is the current page.
    property var entries: []

    // The transition on screen: `incoming` becomes current, `outgoing` leaves.
    property Item incoming: null
    property Item outgoing: null
    property int kind: PageStack.Kind.Forward
    // Where `outgoing` starts from when it was itself mid-transition.
    property real outgoingFromOpacity: 1
    property real outgoingFromX: 0
    property real outgoingFromScale: 1
    // Set while begin() rearranges pages, so stopping the spring does not finish().
    property bool starting: false

    enum Kind {
        Forward,
        Back,
        Fade
    }

    // `transition`: PageStack.Kind.Forward (default) or Fade (e.g. switching sections).
    function push(component: Component, properties: var, transition: int): Item {
        const previous = currentItem;
        const item = create(component, properties ?? {});
        entries = entries.concat([{
                component: component,
                properties: properties ?? {},
                item: item,
                state: undefined
            }]);
        begin(item, transition === PageStack.Kind.Fade ? PageStack.Kind.Fade : PageStack.Kind.Forward,
              previous);
        trim();
        return item;
    }

    function pop(): bool {
        if (entries.length < 2)
            return false;
        const leaving = entries[entries.length - 1];
        // Back during the push of this very page: run that transition in reverse.
        if (progress.target === 1 && incoming === leaving.item && kind === PageStack.Kind.Forward
                && outgoing === entries[entries.length - 2].item) {
            entries = entries.slice(0, -1);
            progress.target = 0;
            return true;
        }
        entries = entries.slice(0, -1);
        const back = entries[entries.length - 1];
        if (!back.item) {
            back.item = create(back.component, back.properties);
            if (back.state !== undefined && typeof back.item.restoreState === "function")
                back.item.restoreState(back.state);
            entries = entries.slice(); // notify currentItem
        }
        begin(back.item, PageStack.Kind.Back, leaving.item);
        return true;
    }

    function replaceAll(component: Component, properties: var): Item {
        const item = create(component, properties ?? {});
        const previous = currentItem;
        for (const entry of entries) {
            if (entry.item && entry.item !== previous && entry.item !== incoming
                    && entry.item !== outgoing)
                entry.item.destroy();
        }
        entries = [{
                component: component,
                properties: properties ?? {},
                item: item,
                state: undefined
            }];
        begin(item, PageStack.Kind.Fade, previous);
        return item;
    }

    function create(component: Component, properties: var): Item {
        const item = component.createObject(root, Object.assign({
            visible: false,
            width: Qt.binding(() => root.width),
            height: Qt.binding(() => root.height)
        }, properties)) as Item;
        if (!item)
            console.warn("PageStack: cannot create page:", component.errorString());
        return item;
    }

    // Pages beyond the cache keep only their component, properties and saved state.
    function trim() {
        const keep = Math.max(1, cachedPages);
        for (let i = 0; i < entries.length - keep; ++i) {
            const entry = entries[i];
            if (!entry.item || entry.item === outgoing || entry.item === incoming
                    || entry.item === currentItem)
                continue;
            if (typeof entry.item.saveState === "function")
                entry.state = entry.item.saveState();
            entry.item.destroy();
            entry.item = null;
        }
    }

    // `from` leaves (null for the first page), `next` arrives.
    function begin(next: Item, transition: int, from: Item) {
        starting = true;

        // Either participant can be obsolete, especially the incoming page of a
        // reversed push. Detach both bindings before settling or reusing the items.
        const oldIncoming = incoming;
        const oldOutgoing = outgoing;
        const fromOpacity = from ? from.opacity : 1;
        const fromX = from ? from.x : 0;
        const fromScale = from ? from.scale : 1;
        incoming = null;
        outgoing = null;
        if (oldIncoming && oldIncoming !== from && oldIncoming !== next)
            settlePage(oldIncoming, false);
        if (oldOutgoing && oldOutgoing !== oldIncoming && oldOutgoing !== from && oldOutgoing !== next)
            settlePage(oldOutgoing, false);

        outgoingFromOpacity = fromOpacity;
        outgoingFromX = fromX;
        outgoingFromScale = fromScale;
        kind = transition;
        outgoing = from;
        incoming = next;
        if (outgoing)
            outgoing.enabled = false;
        if (incoming) {
            incoming.enabled = true;
            incoming.visible = true;
            if (focusIsOurs())
                incoming.forceActiveFocus();
        }
        progress.snapTo(0);
        progress.target = 1;
        starting = false;
    }

    // The keyboard focus is in the stack or nowhere, so a new page may take it. Focus
    // elsewhere (e.g. the title bar's search field, typing) stays where it is.
    function focusIsOurs(): bool {
        for (let item = root.Window.activeFocusItem; item; item = item.parent) {
            if (item === root)
                return true;
        }
        return !root.Window.activeFocusItem || root.Window.activeFocusItem === root.Window.contentItem;
    }

    // Entries own cached pages; transition references own removed pages until
    // they settle. Every retired participant is released through this one path.
    function settlePage(page: Item, shown: bool) {
        if (!page)
            return;
        if (entries.some(entry => entry.item === page))
            resetPage(page, shown);
        else
            page.destroy();
    }

    function resetPage(page: Item, shown: bool) {
        page.visible = shown;
        page.enabled = shown;
        page.opacity = 1;
        page.x = 0;
        page.scale = 1;
    }

    // Called when the spring comes to rest.
    function finish() {
        const arrived = incoming;
        const left = outgoing;
        const forward = progress.target === 1;
        incoming = null;
        outgoing = null;
        settlePage(arrived, forward);
        if (left !== arrived)
            settlePage(left, !forward);
        trim();
    }

    // Reduced motion: a quick cross-fade, no movement.
    readonly property bool reduced: Theme.motion.reduced

    Spring {
        id: progress
        spec: root.reduced ? Theme.motion.fastEffects : Theme.motion.standardDefaultSpatial
        threshold: 0.001
        onRunningChanged: if (!running && !root.starting && (root.incoming || root.outgoing))
                              root.finish()
    }

    // Opacity and geometry of both pages, derived from the one progress value.
    Binding {
        when: root.outgoing !== null
        target: root.outgoing
        property: "opacity"
        value: root.outgoingFromOpacity
               * (root.reduced ? 1 - progress.value
                  : 1 - Theme.motion.segment(progress.value, 0, 0.35))
        restoreMode: Binding.RestoreNone
    }
    Binding {
        when: root.outgoing !== null
        target: root.outgoing
        property: "x"
        value: root.reduced || root.kind === PageStack.Kind.Fade ? root.outgoingFromX
               : Theme.motion.lerp(root.outgoingFromX, root.kind === PageStack.Kind.Forward ? -30 : 30,
                                   progress.value)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        when: root.outgoing !== null
        target: root.outgoing
        property: "scale"
        value: root.outgoingFromScale
        restoreMode: Binding.RestoreNone
    }
    Binding {
        when: root.incoming !== null
        target: root.incoming
        property: "opacity"
        value: root.reduced ? progress.value
               : Theme.motion.segment(progress.value, root.kind === PageStack.Kind.Fade ? 0.35 : 0.2, 1)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        when: root.incoming !== null
        target: root.incoming
        property: "x"
        value: root.reduced || root.kind === PageStack.Kind.Fade ? 0
               : Theme.motion.lerp(root.kind === PageStack.Kind.Forward ? 30 : -30, 0, progress.value)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        when: root.incoming !== null
        target: root.incoming
        property: "scale"
        value: root.reduced || root.kind !== PageStack.Kind.Fade ? 1
               : Theme.motion.lerp(0.92, 1, progress.value)
        restoreMode: Binding.RestoreNone
    }
}
