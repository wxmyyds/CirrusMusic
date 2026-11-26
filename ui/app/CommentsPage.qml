pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The comments of a song, playlist or album (docs/ui/pages/comments.md): the resource
// on top, a box to publish, recommended / hot / newest, then the comments, paged.
// Replies open in place under their comment. A compact bar with the sort and "写评论"
// slides in once the header has scrolled away. Keyboard: the list is one Tab stop;
// ↑/↓ move between comments, Enter opens or closes the replies (or loads more on a
// floor's end), Space unfolds a long text, L likes, R replies, the menu key opens the
// menu. Publishing and replying are hidden for now (`canWrite`).
Item {
    id: page

    required property AppShell shell
    // CommentsPageModel.Target and the resource's key; title, subtitle and cover come
    // from the page that opened this one.
    property int target: CommentsPageModel.Song
    property string targetKey
    property string title
    property string subtitle
    property string coverKey

    readonly property real contentX: Math.max(24, (width - 1200) / 2)
    readonly property real contentWidth: Math.min(width - 48, 1200)
    // Comment text is easier to read short of the full width.
    readonly property real textWidth: 720
    readonly property bool signedIn: App.session.signedIn
    // Publishing and replying: off, NetEase's risk control refuses them from Cirrus
    // (docs/ui/pages/comments.md §10). Liking and deleting stay.
    readonly property bool canWrite: false
    // The comment or reply whose reply box is open.
    property string replyKey
    // The header's publish box (created by the list).
    property TextArea composer: null
    // 0–1 as the header's sort row scrolls under the top: the compact bar slides in.
    readonly property real compactProgress: sortRow
        ? Math.max(0, Math.min(1, (list.contentY - list.originY - sortRow.y + 8) / 56)) : 0
    // The header's sort row (created by the list).
    property Item sortRow: null

    function writeComment(): void {
        if (!canWrite)
            return;
        if (!signedIn) {
            shell.openLogin();
            return;
        }
        list.positionViewAtBeginning();
        composer?.forceActiveFocus();
    }

    // Back to the list (after a box closes): its current row takes the focus.
    function focusList(): void {
        if (list.currentIndex < 0 && list.count > 0)
            list.currentIndex = 0;
        if (list.currentItem)
            list.currentItem.forceActiveFocus();
        else
            list.forceActiveFocus();
    }

    function startReply(row: int): void {
        if (!canWrite)
            return;
        if (!signedIn) {
            shell.openLogin();
            return;
        }
        const rows = pageModel.rows;
        if (row < 0 || row >= rows.count)
            return;
        replyKey = rows.data(rows.index(row, 0), CommentListModel.KeyRole);
    }

    function toggleLike(row: int): void {
        if (!signedIn) {
            shell.openLogin();
            return;
        }
        const rows = pageModel.rows;
        pageModel.setLiked(row, !rows.data(rows.index(row, 0), CommentListModel.LikedRole));
    }

    // The next page once the last rows are near.
    function loadMoreIfNear(): void {
        if (pageModel.canLoadMore && !pageModel.loadingMore && pageModel.loadMoreError.kind < 0
                && list.contentY + list.height > list.contentHeight + list.originY - 800)
            pageModel.loadMore();
    }

    CommentsPageModel {
        id: pageModel
        target: page.target
        targetKey: page.targetKey
        onNotice: message => page.shell.notify(message)
        onPublished: (replyTo, ok) => {
            if (!ok)
                return;
            if (replyTo === "") {
                page.composer.text = "";
                page.focusList();
            } else {
                // Closed and laid out before the reply's rows come in (the model sends
                // this first): rows moving in would otherwise keep the box's room.
                const replying = list.itemAtIndex(pageModel.rows.indexOf(page.replyKey));
                page.replyKey = "";
                (replying as CommentRowItem)?.relayout();
                list.forceLayout();
            }
        }
    }

    // A comment's author picture: round, a person when there is none.
    component Avatar: CoverArt {
        radius: width / 2
        glyph: Symbols.person
    }

    // Recommended, hot, newest. A click breaks the checked bindings; the sort may
    // also change from the other copy (header or compact bar), so follow it here.
    component SortButtons: ConnectedButtonGroup {
        exclusive: true
        Repeater {
            id: sortRepeater
            model: [qsTr("推荐"), qsTr("最热"), qsTr("最新")]
            Button {
                required property int index
                required property string modelData
                variant: Md.Tonal
                size: Md.XSmall
                checkable: true
                checked: pageModel.sort === index
                text: modelData
                onClicked: pageModel.sort = index
            }
        }
        Connections {
            target: pageModel
            function onSortChanged() {
                (sortRepeater.itemAt(pageModel.sort) as Button).checked = true;
            }
        }
    }

    // Writing a comment or a reply: the field, then cancel and send once in use.
    component Composer: Column {
        id: composerBox
        property alias field: field
        property string sendText: qsTr("发表")
        property bool open: field.activeFocus || field.text !== ""
        signal send(string text)
        signal cancel

        spacing: 8 * reveal.value

        TextArea {
            id: field
            width: parent.width
            maximumLines: 6
            readonly property bool tooLong: length > 4096
            error: tooLong
            counterText: length > 3800 ? qsTr("%1 / 4096").arg(length) : ""
            supportingText: tooLong ? qsTr("太长了，删掉一些再发表") : ""
            onSubmitted: composerBox.trySend()
            Keys.onEscapePressed: composerBox.cancel()
            onActiveFocusChanged: {
                if (activeFocus && !page.signedIn) {
                    page.shell.openLogin();
                    page.focusList();
                }
            }
        }
        // Cancel and send unfold under the field once it is in use.
        Item {
            width: parent.width
            height: buttons.implicitHeight * reveal.value
            visible: reveal.value > 0.01
            opacity: Math.min(1, reveal.value)
            clip: reveal.value < 1
            Spring {
                id: reveal
                target: composerBox.open ? 1 : 0
                spec: Theme.motion.standardFastSpatial
                threshold: 0.005
            }
            Row {
                id: buttons
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 8
                Button {
                    variant: Md.Text
                    text: qsTr("取消")
                    onClicked: composerBox.cancel()
                }
                Button {
                    variant: Md.Filled
                    enabled: field.text.trim() !== "" && !field.tooLong && !pageModel.publishing
                    text: composerBox.sendText
                    onClicked: composerBox.trySend()
                }
            }
        }

        function trySend(): void {
            if (field.text.trim() !== "" && !field.tooLong && !pageModel.publishing)
                send(field.text);
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        model: pageModel.rows
        // ListView places its rows at x 0: the margin centers the content.
        leftMargin: page.contentX
        bottomMargin: PageInsets.bottom
        flickableDirection: Flickable.VerticalFlick
        // The current row is the Tab stop (see CommentRowItem), not the list: the list
        // is a focus scope holding the header too, and would hand focus back to it.
        onCountChanged: {
            if (currentIndex < 0 && count > 0)
                currentIndex = 0;
        }
        keyNavigationEnabled: true
        highlightFollowsCurrentItem: false
        cacheBuffer: 800
        boundsBehavior: Flickable.DragAndOvershootBounds
        ScrollBar.vertical: ScrollBar {}
        // How far the view was scrolled, kept from before the header changes height.
        property real scrolled: 0
        onContentYChanged: {
            scrolled = contentY - originY;
            page.loadMoreIfNear();
        }
        onContentHeightChanged: Qt.callLater(page.loadMoreIfNear)
        Accessible.role: Accessible.List
        Accessible.name: qsTr("评论")

        // New rows (a page, opened replies, what was just published) fade and rise in;
        // a deleted comment fades out; the rows below close the gap, and make room for
        // opened replies, smoothly.
        populate: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.defaultEffects.duration
                easing: Theme.motion.defaultEffects.easing
            }
        }
        add: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.defaultEffects.duration
                easing: Theme.motion.defaultEffects.easing
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1
                duration: Theme.motion.defaultSpatial.duration
                easing: Theme.motion.defaultSpatial.easing
            }
        }
        remove: Transition {
            NumberAnimation {
                property: "opacity"
                to: 0
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
        }
        displaced: Transition {
            NumberAnimation {
                property: "y"
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
        }

        Keys.onPressed: event => {
            // Only with the list itself focused: typing in a box goes there.
            if (Window.activeFocusItem === null || Window.activeFocusItem !== list.currentItem)
                return;
            const row = list.currentIndex;
            const rows = pageModel.rows;
            if (row < 0 || row >= rows.count)
                return;
            const index = rows.index(row, 0);
            const type = rows.data(index, CommentListModel.RowTypeRole);
            const usable = type !== CommentListModel.RepliesEnd
                           && !rows.data(index, CommentListModel.DeletedRole)
                           && !rows.data(index, CommentListModel.LocalRole);
            switch (event.key) {
            case Qt.Key_L:
                if (usable)
                    page.toggleLike(row);
                break;
            case Qt.Key_R:
                if (usable && page.canWrite)
                    page.startReply(row);
                break;
            case Qt.Key_Return:
            case Qt.Key_Enter:
                if (type === CommentListModel.RepliesEnd) {
                    if (rows.data(index, CommentListModel.HasMoreRole)
                            || rows.data(index, CommentListModel.FailedRole))
                        pageModel.moreReplies(row);
                    else
                        pageModel.collapseReplies(row);
                } else if (type === CommentListModel.Comment) {
                    if (rows.data(index, CommentListModel.ExpandedRole))
                        pageModel.collapseReplies(row);
                    else if (rows.data(index, CommentListModel.ReplyCountRole) > 0)
                        pageModel.expandReplies(row);
                }
                break;
            case Qt.Key_Space:
                (list.currentItem as CommentRowItem)?.toggleFold();
                break;
            case Qt.Key_Menu:
                if (usable)
                    (list.currentItem as CommentRowItem)?.openMenu();
                break;
            case Qt.Key_F10:
                if (usable && (event.modifiers & Qt.ShiftModifier))
                    (list.currentItem as CommentRowItem)?.openMenu();
                else
                    return;
                break;
            default:
                return;
            }
            event.accepted = true;
        }

        header: Column {
            id: header
            width: page.contentWidth
            // ListView keeps its first row in place when the header grows (the box
            // opening): at the top, stay at the top instead.
            onHeightChanged: {
                if (list.scrolled < 1)
                    list.positionViewAtBeginning();
            }
            topPadding: 24
            bottomPadding: 16
            spacing: 24

            // The resource the comments are about.
            Item {
                width: parent.width
                height: 72
                CoverArt {
                    id: resourceCover
                    width: 72
                    height: 72
                    radius: Theme.shape.medium
                    coverKey: page.coverKey
                    glyph: page.target === CommentsPageModel.Album ? Symbols.album
                         : page.target === CommentsPageModel.Playlist ? Symbols.queue_music
                         : Symbols.music_note
                }
                Column {
                    anchors.left: resourceCover.right
                    anchors.leftMargin: 16
                    anchors.right: playButton.visible ? playButton.left : parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: [qsTr("歌曲评论"), qsTr("歌单评论"), qsTr("专辑评论")][page.target]
                        color: Theme.color.primary
                        font: Theme.type.labelLarge.font
                    }
                    Text {
                        id: resourceTitle
                        // A song's title opens its page (song.md §3).
                        readonly property bool linked: page.target === CommentsPageModel.Song
                        width: Math.min(parent.width, implicitWidth)
                        text: page.title
                        elide: Text.ElideRight
                        color: linked && titleHover.hovered ? Theme.color.primary
                                                            : Theme.color.onSurface
                        font: Theme.type.titleLargeEmphasized.font
                        Accessible.role: Accessible.Heading
                        HoverHandler {
                            id: titleHover
                            enabled: resourceTitle.linked
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            enabled: resourceTitle.linked
                            onTapped: page.shell.openSong(page.targetKey)
                        }
                    }
                    Text {
                        width: parent.width
                        visible: text !== ""
                        text: [page.subtitle,
                               pageModel.total >= 0 ? qsTr("共 %1 条").arg(App.format.count(pageModel.total))
                                                    : ""].filter(part => part !== "").join(" · ")
                        elide: Text.ElideRight
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }
                }
                IconButton {
                    id: playButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: page.target === CommentsPageModel.Song
                    variant: Md.Tonal
                    glyph: Symbols.play_arrow
                    iconFill: 1
                    text: qsTr("播放%1").arg(page.title)
                    onClicked: App.player.playSongs([page.targetKey], 0, Playback.FromSong,
                                                     page.targetKey)
                }
            }

            // Publishing: the account's picture and the box.
            Item {
                width: Math.min(parent.width, page.textWidth + 56)
                height: composerBox.height
                visible: page.canWrite
                Avatar {
                    width: 40
                    height: 40
                    y: 8
                    coverKey: App.session.avatarKey
                }
                Composer {
                    id: composerBox
                    x: 56
                    width: parent.width - 56
                    field.placeholderText: page.signedIn ? qsTr("写下你的评论…")
                                                         : qsTr("登录后参与评论")
                    Component.onCompleted: page.composer = field
                    onSend: text => pageModel.publish(text)
                    onCancel: {
                        if (field.text.trim() === "") {
                            field.text = "";
                            page.focusList();
                        } else {
                            discardDialog.open();
                        }
                    }
                }
            }

            Item {
                id: headerSort
                width: parent.width
                height: 32
                Component.onCompleted: page.sortRow = headerSort
                SortButtons {}
            }

            TabState {
                model: pageModel
                emptyGlyph: Symbols.comment
                emptyText: page.canWrite ? qsTr("还没有评论，来写第一条吧") : qsTr("还没有评论")
                skeleton: Column {
                    width: page.contentWidth
                    spacing: 24
                    Repeater {
                        model: 3
                        Item {
                            width: parent.width
                            height: 64
                            Skeleton {
                                width: 40
                                height: 40
                                radius: 20
                            }
                            Skeleton {
                                x: 56
                                width: 120
                                height: 14
                            }
                            Skeleton {
                                x: 56
                                y: 26
                                width: Math.min(parent.width - 56, page.textWidth)
                                height: 14
                            }
                            Skeleton {
                                x: 56
                                y: 48
                                width: Math.min(parent.width - 56, page.textWidth) * 0.6
                                height: 14
                            }
                        }
                    }
                }
            }
        }

        delegate: CommentRowItem {}

        footer: LoadMoreFooter {
            width: page.contentWidth
            model: pageModel
        }
    }

    // One row: a comment, a reply (indented), or the end of a comment's replies.
    component CommentRowItem: Item {
        id: row

        required property int index
        required property int rowType
        required property string key
        required property string authorName
        required property string authorKey
        required property string avatarKey
        required property real timeMs
        required property string text
        required property bool deleted
        required property bool hasReplyTo
        required property string replyToName
        required property string replyToText
        required property real likedCount
        required property bool liked
        required property real replyCount
        required property bool mine
        required property bool local
        required property bool expanded
        required property bool hasMore
        required property bool loading
        required property bool failed

        readonly property bool isEnd: rowType === CommentListModel.RepliesEnd
        readonly property bool isReply: rowType === CommentListModel.Reply
        readonly property real indent: isReply || isEnd ? 56 : 0
        readonly property real avatarSize: isReply ? 32 : 40
        readonly property bool usable: !isEnd && !deleted && !local
        // The author's page opens from the avatar, the name and the menu.
        readonly property bool hasAuthor: !isEnd && authorKey !== ""
        // The row itself, not its reply box.
        readonly property bool focused: activeFocus
                                        && !((replyLoader.item as Composer)?.field.activeFocus ?? false)
        // The ring only for the keyboard: a click also hands the row the focus back
        // (a box closing, a menu returning it).
        readonly property bool showsFocus: InputMode.keyboard && focused
        property bool unfolded: false

        function toggleFold(): void {
            if (!body.folds)
                return;
            fold.snapTo(body.height);
            unfolded = !unfolded;
            fold.target = body.restHeight;
        }
        // Takes its new height now rather than at the next frame.
        function relayout(): void {
            content.forceLayout();
        }
        function openMenu(): void {
            commentMenu.openFor(row.index, row.key, row.text, row.mine, row.authorKey, actions,
                                more.x, more.y + more.height);
        }

        width: page.contentWidth
        height: isEnd ? 48 : content.height + 16
        // An open reply box is scrolled into view as the row grows for it.
        onHeightChanged: {
            if (replyLoader.active)
                Qt.callLater(() => list.positionViewAtIndex(row.index, ListView.Contain));
        }
        // One Tab stop for the list: the current row (kept until the focus has left,
        // which Qt requires).
        activeFocusOnTab: ListView.isCurrentItem || activeFocus

        TapHandler {
            // A click makes it the current row, for the keyboard.
            onTapped: list.currentIndex = row.index
        }
        StateLayer {
            x: row.indent - 8
            width: Math.min(row.width - x, page.textWidth + 72)
            height: parent.height
            radius: Theme.shape.large
            color: Theme.color.onSurface
            visible: !row.isEnd || row.showsFocus
            hovered: hover.hovered
            focused: row.showsFocus

            HoverHandler {
                id: hover
                enabled: !row.isEnd
            }
            FocusRing {
                shown: row.showsFocus
                containerRadius: Theme.shape.large
            }
        }

        // The end of a floor: more, collapse, loading or a failure.
        Row {
            x: row.indent + 44
            anchors.verticalCenter: parent.verticalCenter
            visible: row.isEnd
            spacing: 8
            LoadingIndicator {
                implicitWidth: 32
                implicitHeight: 32
                anchors.verticalCenter: parent.verticalCenter
                visible: row.loading
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: row.failed
                text: qsTr("回复没有加载出来")
                color: Theme.color.error
                font: Theme.type.bodyMedium.font
            }
            Button {
                anchors.verticalCenter: parent.verticalCenter
                visible: (row.hasMore || row.failed) && !row.loading
                focusPolicy: Qt.NoFocus
                variant: Md.Text
                size: Md.XSmall
                trailingGlyph: row.failed ? "" : Symbols.expand_more
                text: row.failed ? qsTr("重试") : qsTr("更多回复")
                onClicked: pageModel.moreReplies(row.index)
            }
            Button {
                anchors.verticalCenter: parent.verticalCenter
                visible: !row.loading
                focusPolicy: Qt.NoFocus
                variant: Md.Text
                size: Md.XSmall
                trailingGlyph: Symbols.expand_less
                text: qsTr("收起回复")
                onClicked: pageModel.collapseReplies(row.index)
            }
        }

        Avatar {
            x: row.indent
            y: 8
            width: row.avatarSize
            height: row.avatarSize
            visible: !row.isEnd
            coverKey: row.avatarKey
            MouseArea {
                anchors.fill: parent
                enabled: row.hasAuthor
                cursorShape: Qt.PointingHandCursor
                onClicked: page.shell.openUser(row.authorKey)
            }
        }

        Column {
            id: content
            x: row.indent + row.avatarSize + 16
            y: 8
            // Replies end where their comment's text does.
            width: Math.min(row.width - x, page.textWidth - row.indent)
            visible: !row.isEnd
            spacing: 6

            Row {
                width: parent.width
                spacing: 8
                Text {
                    width: Math.min(implicitWidth, parent.width - when.width - 8)
                    text: row.authorName
                    elide: Text.ElideRight
                    color: Theme.color.onSurface
                    font: Theme.type.titleSmall.font
                    MouseArea {
                        anchors.fill: parent
                        enabled: row.hasAuthor
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.shell.openUser(row.authorKey)
                    }
                }
                Text {
                    id: when
                    anchors.baseline: parent.children[0].baseline
                    text: App.format.relativeTime(row.timeMs)
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.bodySmall.font
                }
            }

            Text {
                width: parent.width
                visible: row.deleted
                text: qsTr("这条评论已删除")
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }

            // The text: plain, selectable, folded past 8 lines.
            Item {
                id: body
                // Fold the text using the text engine's measured line pitch.
                readonly property real lineHeight: textEdit.lineCount > 0
                    ? textEdit.contentHeight / textEdit.lineCount : 0
                readonly property bool folds: textEdit.lineCount > 8
                width: parent.width
                readonly property real restHeight: folds && !row.unfolded ? lineHeight * 8
                                                                         : textEdit.contentHeight
                // Unfolding and folding slide (see toggleFold()).
                height: fold.running ? fold.value : restHeight
                visible: !row.deleted
                clip: true
                Spring {
                    id: fold
                    spec: Theme.motion.standardDefaultSpatial
                    threshold: 0.5
                }
                SelectableText {
                    id: textEdit
                    width: parent.width
                    activeFocusOnPress: false
                    text: row.text
                    color: Theme.color.onSurface
                }
            }
            Button {
                visible: body.folds && !row.deleted
                focusPolicy: Qt.NoFocus
                variant: Md.Text
                size: Md.XSmall
                text: row.unfolded ? qsTr("收起") : qsTr("展开")
                onClicked: row.toggleFold()
            }

            // The comment replied to, quoted.
            Rectangle {
                width: parent.width
                height: quote.implicitHeight + 16
                visible: row.hasReplyTo && !row.deleted
                radius: Theme.shape.small
                color: Theme.color.surfaceContainer
                Text {
                    id: quote
                    x: 12
                    y: 8
                    width: parent.width - 24
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: qsTr("回复 @%1：%2").arg(row.replyToName)
                        .arg(row.replyToText !== "" ? row.replyToText : qsTr("原评论已删除"))
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.bodyMedium.font
                }
            }

            Text {
                width: parent.width
                visible: row.local
                text: qsTr("刚发表，刷新后可以点赞和回复")
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodySmall.font
            }

            // Like, reply, more.
            Row {
                id: actions
                visible: row.usable
                spacing: 4
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    focusPolicy: Qt.NoFocus
                    size: Md.XSmall
                    // A like pops: shrinks, then springs back past its size.
                    scale: pop.value
                    Spring {
                        id: pop
                        target: 1
                        spec: Theme.motion.fastSpatial
                        threshold: 0.002
                    }
                    Connections {
                        target: row
                        function onLikedChanged() {
                            if (row.liked) {
                                pop.snapTo(0.6);
                                pop.target = 1;
                            }
                        }
                    }
                    glyph: Symbols.thumb_up
                    iconFill: row.liked ? 1 : 0
                    checkable: false
                    text: row.liked ? qsTr("取消点赞") : qsTr("点赞")
                    onClicked: page.toggleLike(row.index)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(implicitWidth, 24)
                    text: row.likedCount > 0 ? App.format.count(row.likedCount) : ""
                    color: row.liked ? Theme.color.primary : Theme.color.onSurfaceVariant
                    font: Theme.type.labelMedium.font
                }
                Button {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: page.canWrite
                    focusPolicy: Qt.NoFocus
                    variant: Md.Text
                    size: Md.XSmall
                    text: qsTr("回复")
                    onClicked: page.startReply(row.index)
                }
                IconButton {
                    id: more
                    anchors.verticalCenter: parent.verticalCenter
                    focusPolicy: Qt.NoFocus
                    size: Md.XSmall
                    glyph: Symbols.more_vert
                    text: qsTr("更多")
                    onClicked: row.openMenu()
                }
            }

            // The replies under a comment, until opened.
            Button {
                visible: row.rowType === CommentListModel.Comment && !row.expanded
                         && row.replyCount > 0
                focusPolicy: Qt.NoFocus
                variant: Md.Text
                size: Md.XSmall
                trailingGlyph: Symbols.expand_more
                text: qsTr("展开 %1 条回复").arg(App.format.count(row.replyCount))
                onClicked: pageModel.expandReplies(row.index)
            }

            // Replying to this one: the box unfolds (and closes at once, so rows moved
            // under it land in place).
            Item {
                width: parent.width
                height: ((replyLoader.item as Composer)?.height ?? 0) * replyOpen.value
                visible: replyLoader.active
                opacity: Math.min(1, replyOpen.value)
                clip: replyOpen.value < 1
                Spring {
                    id: replyOpen
                    spec: Theme.motion.standardFastSpatial
                    threshold: 0.005
                }
                Loader {
                    id: replyLoader
                    width: parent.width
                    active: page.replyKey !== "" && page.replyKey === row.key
                    onActiveChanged: {
                        replyOpen.snapTo(0);
                        replyOpen.target = active ? 1 : 0;
                    }
                    // Open already when the row is created (scrolled back into view).
                    Component.onCompleted: replyOpen.snapTo(active ? 1 : 0)
                    sourceComponent: Composer {
                        id: replyBox
                        width: parent ? parent.width : 0
                        open: true
                        sendText: qsTr("回复")
                        field.placeholderText: qsTr("回复 @%1").arg(row.authorName)
                        Component.onCompleted: field.forceActiveFocus()
                        onSend: text => pageModel.reply(pageModel.rows.indexOf(row.key), text)
                        onCancel: {
                            page.replyKey = "";
                            list.currentIndex = row.index;
                            page.focusList();
                        }
                    }
                }
            }
        }

        Accessible.role: Accessible.ListItem
        Accessible.name: isEnd ? qsTr("回复列表末尾")
            : deleted ? qsTr("这条评论已删除")
            : [authorName, App.format.relativeTime(timeMs), text,
               likedCount > 0 ? qsTr("%1 个赞").arg(App.format.count(likedCount)) : "",
               replyCount > 0 ? qsTr("%1 条回复").arg(App.format.count(replyCount)) : ""]
                .filter(part => part !== "").join("，")
    }

    // Slides in, following the scroll, once the sort row has gone.
    Rectangle {
        id: compactBar
        width: parent.width - 12
        height: 56
        y: -height * (1 - page.compactProgress)
        visible: page.compactProgress > 0
        color: Theme.color.surface

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
        }
        HoverHandler {
            blocking: true
        }
        Item {
            x: page.contentX
            width: page.contentWidth
            height: parent.height
            Text {
                width: compactSort.x - 16
                anchors.verticalCenter: parent.verticalCenter
                text: page.title
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: Theme.type.titleMedium.font
            }
            SortButtons {
                id: compactSort
                x: (writeButton.visible ? writeButton.x - 16 : parent.width) - width
                anchors.verticalCenter: parent.verticalCenter
            }
            Button {
                id: writeButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: page.canWrite
                variant: Md.Tonal
                size: Md.XSmall
                glyph: Symbols.edit
                text: qsTr("写评论")
                onClicked: page.writeComment()
            }
        }
    }

    // Copy, the author's page, reply (when allowed), delete (the account's own).
    Menu {
        id: commentMenu
        property int row: -1
        property string key
        property string text
        property bool mine: false
        property string author

        function openFor(target: int, commentKey: string, body: string, own: bool,
                         authorKey: string, anchor: Item, x: real, y: real): void {
            row = target;
            key = commentKey;
            text = body;
            mine = own;
            author = authorKey;
            list.currentIndex = target;
            popup(anchor, x, y);
        }

        MenuItem {
            glyph: Symbols.content_copy
            text: qsTr("复制")
            onTriggered: App.copyText(commentMenu.text)
        }
        MenuItem {
            visible: commentMenu.author !== ""
            height: visible ? implicitHeight : 0
            glyph: Symbols.person
            text: qsTr("查看用户主页")
            onTriggered: page.shell.openUser(commentMenu.author)
        }
        MenuItem {
            visible: page.canWrite
            height: visible ? implicitHeight : 0
            glyph: Symbols.comment
            text: qsTr("回复")
            trailingText: "R"
            onTriggered: page.startReply(commentMenu.row)
        }
        MenuItem {
            visible: commentMenu.mine
            height: visible ? implicitHeight : 0
            glyph: Symbols.delete_
            text: qsTr("删除")
            onTriggered: deleteDialog.open()
        }
    }

    Dialog {
        id: deleteDialog
        title: qsTr("删除这条评论？")
        supportingText: qsTr("删除后不能恢复。")
        acceptText: qsTr("删除")
        rejectText: qsTr("取消")
        onAccepted: pageModel.remove(pageModel.rows.indexOf(commentMenu.key))
    }

    Dialog {
        id: discardDialog
        title: qsTr("放弃这条评论？")
        supportingText: qsTr("写下的内容不会保存。")
        acceptText: qsTr("放弃")
        rejectText: qsTr("继续写")
        onAccepted: {
            page.composer.text = "";
            page.focusList();
        }
    }
}
