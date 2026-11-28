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
// transition, and a new navigation mid-way starts from what is on screen. The old page
// remains current and interactive while a destination is incubated. The newest `cachedPages`
// pages stay alive (keeping scroll
// position); older ones are destroyed and recreated when navigated back to, with
// `saveState()` / `restoreState(state)` if the page defines them.
//   PageStack { id: stack; Component.onCompleted: replaceAll(homePage) }
//   stack.push(playlistPage, { playlistKey: key })
Item {
    id: root

    property int cachedPages: 4
    readonly property int depth: entries.length
    // The page actually on screen. Pending entries must not become current before
    // their transition starts; this also keeps the old page interactive during loading.
    property Item displayed: null
    readonly property Item currentItem: displayed
    readonly property bool creating: pendingIncubations > 0
    property int pendingIncubations: 0
    property int lastToken: 0
    property bool lastPushPending: false
    property bool lastPushFailed: false

    // {component, properties, item, state, token, transition, incubator, activation,
    // canceled}; the last entry is the requested destination, even while incubating.
    property var entries: []
    signal pageReady(var page, int token)
    // The entry remains in history, but its card-to-page flight was superseded.
    signal pageHandoffDiscarded(int token)
    // The entry itself was removed or failed to create.
    signal pageDiscarded(int token, bool failed)

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
        const kind = transition === PageStack.Kind.Fade ? PageStack.Kind.Fade
                                                         : PageStack.Kind.Forward;
        const entry = {
            component: component,
            properties: properties ?? {},
            item: null,
            state: undefined,
            token: ++lastToken,
            transition: kind,
            incubator: null,
            activation: null,
            canceled: false,
            failed: false
        };
        entries = entries.concat([entry]);
        lastPushPending = false;
        lastPushFailed = false;
        startIncubation(entry);
        lastPushPending = !!entry.incubator;
        lastPushFailed = entry.failed;
        trim();
        return entry.item;
    }

    function pop(): bool {
        if (entries.length < 2)
            return false;
        const leaving = entries[entries.length - 1];
        // A destination that has not finished incubating was never shown. Remove it
        // without starting a reverse animation; the already-visible page remains current.
        if (!leaving.item) {
            discardEntry(leaving, PageStack.Kind.Back, false);
            return true;
        }
        // Back during the push of this very page: run that transition in reverse.
        if (progress.target === 1 && incoming === leaving.item && kind === PageStack.Kind.Forward
                && outgoing === entries[entries.length - 2].item) {
            const back = entries[entries.length - 2];
            entries = entries.slice(0, -1);
            displayed = back.item;
            progress.target = 0;
            return true;
        }
        entries = entries.slice(0, -1);
        const back = entries[entries.length - 1];
        if (back.item) {
            activate(back, PageStack.Kind.Back, leaving.item);
        } else {
            // Reuse an in-flight page instead of creating a second copy. Completion
            // will activate it from the page that is still visible.
            back.activation = {
                transition: PageStack.Kind.Back,
                from: leaving.item
            };
            if (back.incubator) {
                back.incubator.forceCompletion();
            } else {
                back.item = create(back.component, back.properties);
                if (!back.item) {
                    back.activation = null;
                    discardEntry(back, PageStack.Kind.Back, true);
                    return true;
                }
                if (back.state !== undefined && typeof back.item.restoreState === "function")
                    back.item.restoreState(back.state);
                const activation = back.activation;
                back.activation = null;
                entries = entries.slice();
                activate(back, activation.transition, activation.from);
            }
        }
        return true;
    }

    function replaceAll(component: Component, properties: var): Item {
        const previous = displayed;
        const discarded = [];
        for (const entry of entries) {
            if (entry.incubator) {
                cancelIncubation(entry);
                discarded.push(entry.token);
            }
            if (entry.item && entry.item !== previous && entry.item !== incoming
                    && entry.item !== outgoing)
                entry.item.destroy();
        }
        const item = create(component, properties ?? {});
        entries = [{
                component: component,
                properties: properties ?? {},
                item: item,
                state: undefined,
                token: 0,
                transition: PageStack.Kind.Fade,
                incubator: null,
                activation: null,
                canceled: false,
                failed: false
            }];
        displayed = item;
        begin(item, PageStack.Kind.Fade, previous);
        for (const token of discarded)
            pageDiscarded(token, false);
        return item;
    }

    function create(component: Component, properties: var): Item {
        const item = component.createObject(root, initialProperties(properties)) as Item;
        if (!item)
            console.warn("PageStack: cannot create page:", component.errorString());
        return item;
    }

    function initialProperties(properties: var): var {
        return Object.assign({
                visible: false,
                width: Qt.binding(() => root.width),
                height: Qt.binding(() => root.height)
            }, properties ?? {});
    }

    // QQuickWindow installs a QQmlIncubationController, so complex pages are built in
    // idle slices instead of blocking the frame in component.createObject(). The old page
    // remains current until the destination is ready.
    function startIncubation(entry: var): void {
        const incubator = entry.component.incubateObject(root, initialProperties(entry.properties));
        if (!incubator) {
            discardEntry(entry, null, true);
            return;
        }
        entry.incubator = incubator;
        ++pendingIncubations;
        entries = entries.slice();

        const completed = status => {
            if (entry.incubator !== incubator || status === Component.Loading)
                return;
            entry.incubator = null;
            --pendingIncubations;
            const item = incubator.object as Item;
            if (entry.canceled || entries.indexOf(entry) < 0) {
                if (item)
                    item.destroy();
                return;
            }
            if (status !== Component.Ready || !item) {
                discardEntry(entry, null, true);
                return;
            }

            entry.item = item;
            if (entry.state !== undefined && typeof item.restoreState === "function")
                item.restoreState(entry.state);
            entries = entries.slice();
            if (entries[entries.length - 1] === entry) {
                const activation = entry.activation;
                entry.activation = null;
                activate(entry, activation ? activation.transition : entry.transition,
                         activation ? activation.from : displayed);
            } else {
                // It finished behind a newer destination. Keep it as a hidden cache entry,
                // but a card flight for that abandoned navigation must not wait forever.
                item.visible = false;
                entry.activation = null;
                trim();
                pageHandoffDiscarded(entry.token);
            }
        };
        incubator.onStatusChanged = completed;
        // Without a controller (for example an offscreen QML test), incubation may
        // already have completed before the callback is attached.
        if (incubator.status !== Component.Loading)
            completed(incubator.status);
    }

    function activate(entry: var, transition: int, from: Item): void {
        if (!entry.item)
            return;
        const previous = from || displayed;
        displayed = entry.item;
        entries = entries.slice();
        begin(entry.item, transition, previous);
        trim();
        pageReady(entry.item, entry.token);
    }

    // QML exposes no abort operation for the incubator returned by Component. Keep its
    // completion callback attached and mark it canceled; a late object is destroyed there.
    function cancelIncubation(entry: var): void {
        if (entry.incubator)
            entry.canceled = true;
    }

    function discardEntry(entry: var, revealTransition: var, failed: bool): void {
        const index = entries.indexOf(entry);
        if (index < 0)
            return;
        cancelIncubation(entry);
        entry.failed = failed;
        entries = entries.slice(0, index).concat(entries.slice(index + 1));
        pageDiscarded(entry.token, failed);
        ensureTopActive(revealTransition);
    }

    // A newer pending navigation can hide a page that finished first. If that newer
    // navigation is removed or fails, reveal the now-current stack top instead of
    // leaving a cached but invisible page as the logical destination.
    function ensureTopActive(revealTransition: var): void {
        if (entries.length === 0)
            return;
        const top = entries[entries.length - 1];
        if (top.item && top.item !== displayed)
            activate(top, revealTransition === undefined || revealTransition === null
                     ? PageStack.Kind.Back : revealTransition, displayed);
    }

    // Pages beyond the cache keep only their component, properties and saved state.
    function trim() {
        const keep = Math.max(1, cachedPages);
        let changed = false;
        for (let i = 0; i < entries.length - keep; ++i) {
            const entry = entries[i];
            // Let an old incubator finish; its completion will be destroyed here on the
            // next trim pass. Removing its callback would leak the eventual object.
            if (!entry.item || entry.item === outgoing || entry.item === incoming
                    || entry.item === displayed)
                continue;
            if (typeof entry.item.saveState === "function")
                entry.state = entry.item.saveState();
            entry.item.destroy();
            entry.item = null;
            changed = true;
        }
        if (changed)
            entries = entries.slice();
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
