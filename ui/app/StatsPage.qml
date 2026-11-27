pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Listening statistics (docs/ui/pages/stats.md): this computer's plays. A range (week,
// month, year, all; paged back with ‹ › or [ ]) scopes everything under it: how much
// (the headline time with its trend, four counts), what (songs, artists, albums), when
// (the weekday × hour grid) and from where. The streak is all time, so it sits above the
// range. Reloads by itself when a play is recorded.
Item {
    id: page

    required property AppShell shell

    readonly property bool wide: contentWidth >= 792
    readonly property real contentX: Math.max(24, (width - 1200) / 2)
    readonly property real contentWidth: Math.min(width - 48, 1200)
    readonly property bool hasPeriod: !pageModel.periodEmpty

    StatsPageModel {
        id: pageModel
        range: App.settings.statsRange
        byTime: App.settings.statsByTime
    }

    function openOrigin(row: var): void {
        switch (row.kind) {
        case StatsPageModel.Playlist:
        case StatsPageModel.Heartbeat:
            shell.openPlaylist(row.key);
            break;
        case StatsPageModel.Album:
            shell.openAlbum(row.key);
            break;
        case StatsPageModel.Artist:
            shell.openArtist(row.key);
            break;
        case StatsPageModel.Ranking:
            if (row.own)
                shell.openRanking();
            else
                shell.openUser(row.key);
            break;
        }
    }

    // [ and ] page through periods (single keys stay text while typing: ShortcutMap).
    Shortcut {
        sequences: App.shortcuts.keys.statsPrevious
        enabled: page.visible
        onActivated: pageModel.goBack()
    }
    Shortcut {
        sequences: App.shortcuts.keys.statsNext
        enabled: page.visible
        onActivated: pageModel.goForward()
    }

    PageState {
        id: pageState
        anchors.fill: parent
        model: pageModel
        emptyGlyph: Symbols.bar_chart
        emptyText: qsTr("还没有听歌记录")
        skeleton: Column {
            x: page.contentX
            y: 24
            width: page.contentWidth
            spacing: 24
            Skeleton {
                width: 160
                height: 32
            }
            Skeleton {
                width: parent.width
                height: 240
                radius: Theme.shape.extraLarge
            }
            Row {
                spacing: 16
                Repeater {
                    model: 4
                    Skeleton {
                        width: (page.contentWidth - 48) / 4
                        height: 96
                        radius: Theme.shape.large
                    }
                }
            }
        }
    }

    component SectionTitle: Text {
        color: Theme.color.onSurface
        font: Theme.type.titleLargeEmphasized.font
        Accessible.role: Accessible.Heading
    }
    component SubTitle: Text {
        color: Theme.color.onSurface
        font: Theme.type.titleMedium.font
        Accessible.role: Accessible.Heading
    }
    // Rows of an artist, album or origin ranking.
    component Ranking: Column {
        id: ranking
        property var rows: []
        property bool covers: false
        property bool round: false
        property string glyph
        signal picked(var row)
        width: parent ? parent.width : 0
        spacing: 2
        Repeater {
            model: ranking.rows
            delegate: RankRow {
                required property var modelData
                required property int index
                width: ranking.width
                rank: index + 1
                text: modelData.title
                coverKey: modelData.coverKey
                showCover: ranking.covers
                roundCover: ranking.round
                glyph: ranking.glyph
                fraction: modelData.fraction
                valueText: modelData.text
                clickable: modelData.clickable
                onClicked: ranking.picked(modelData)
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        opacity: pageState.contentOpacity
        visible: opacity > 0
        contentHeight: content.implicitHeight + 48
        bottomMargin: PageInsets.bottom
        boundsBehavior: Flickable.DragAndOvershootBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: content
            x: page.contentX
            y: 24
            width: page.contentWidth
            spacing: 24

            // The sections rise in one after another when the records first arrive.
            Reveal {
                target: mainCard
                shown: pageState.showsContent
            }
            Reveal {
                target: counts
                shown: pageState.showsContent
                order: 1
            }
            Reveal {
                target: mostTitle
                shown: pageState.showsContent
                order: 2
            }
            Reveal {
                target: most
                shown: pageState.showsContent
                order: 2
            }
            Reveal {
                target: whenWhere
                shown: pageState.showsContent
                order: 3
            }

            // The title, where the records come from, and the streak (all time).
            Item {
                width: parent.width
                height: page.wide ? Math.max(titles.height, streakCard.height)
                                  : titles.height + (streakCard.visible ? streakCard.height + 16 : 0)
                Column {
                    id: titles
                    width: parent.width - (page.wide && streakCard.visible ? streakCard.width + 24 : 0)
                    spacing: 4
                    Text {
                        text: qsTr("听歌统计")
                        color: Theme.color.onSurface
                        font: Theme.type.headlineMediumEmphasized.font
                        Accessible.role: Accessible.Heading
                    }
                    Text {
                        width: parent.width
                        text: (App.session.signedIn ? qsTr("这台电脑上的播放记录")
                                                    : qsTr("游客的播放记录"))
                              + (pageModel.sinceText !== "" ? " · " + pageModel.sinceText : "")
                        elide: Text.ElideRight
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }
                }
                Rectangle {
                    id: streakCard
                    visible: pageModel.longestStreak > 0
                    x: page.wide ? parent.width - width : 0
                    y: page.wide ? 0 : titles.height + 16
                    width: streakRow.implicitWidth + 40
                    height: 72
                    radius: Theme.shape.large
                    color: Theme.color.tertiaryContainer
                    Accessible.role: Accessible.StaticText
                    Accessible.name: streakTitle.text + "，" + streakNote.text
                    Row {
                        id: streakRow
                        anchors.centerIn: parent
                        spacing: 12
                        IconContainer {
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Symbols.local_fire_department
                            size: 40
                            tone: Md.Tertiary
                            inverted: true
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                id: streakTitle
                                text: pageModel.streak > 0
                                      ? qsTr("连续 %1 天").arg(pageModel.streak)
                                      : qsTr("最长连续 %1 天").arg(pageModel.longestStreak)
                                color: Theme.color.onTertiaryContainer
                                font: Theme.type.titleMedium.font
                            }
                            Text {
                                id: streakNote
                                visible: pageModel.streak > 0
                                text: qsTr("最长 %1 天").arg(pageModel.longestStreak)
                                color: Theme.color.onTertiaryContainer
                                font: Theme.type.bodySmall.font
                            }
                        }
                    }
                }
            }

            // The range: everything under it follows.
            Flow {
                width: parent.width
                spacing: 16
                ConnectedButtonGroup {
                    exclusive: true
                    Repeater {
                        model: [qsTr("周"), qsTr("月"), qsTr("年"), qsTr("全部")]
                        delegate: Button {
                            required property string modelData
                            required property int index
                            variant: Md.Tonal
                            checkable: true
                            checked: pageModel.range === index
                            text: modelData
                            onClicked: App.settings.statsRange = index
                        }
                    }
                }
                Row {
                    visible: pageModel.range !== StatsPageModel.AllTime
                    height: 40
                    spacing: 4
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Symbols.chevron_left
                        text: qsTr("上一段")
                        enabled: pageModel.canGoBack
                        onClicked: pageModel.goBack()
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(implicitWidth, 120)
                        horizontalAlignment: Text.AlignHCenter
                        text: pageModel.periodTitle
                        color: Theme.color.onSurface
                        font: Theme.type.titleMedium.font
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Symbols.chevron_right
                        text: qsTr("下一段")
                        enabled: pageModel.canGoForward
                        onClicked: pageModel.goForward()
                    }
                    Button {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: pageModel.currentText !== ""
                        variant: Md.Text
                        text: pageModel.currentText
                        onClicked: pageModel.goToCurrent()
                    }
                }
            }

            // How much: the headline time and its trend.
            Rectangle {
                id: mainCard
                width: parent.width
                height: page.wide ? 260 : headline.height + 180 + 72
                radius: Theme.shape.extraLarge
                color: Theme.color.primaryContainer

                Column {
                    id: headline
                    x: 28
                    y: 28
                    width: page.wide ? parent.width * 0.36 : parent.width - 56
                    spacing: 6
                    Text {
                        text: pageModel.headline
                        color: Theme.color.onPrimaryContainer
                        font: Theme.type.titleMedium.font
                    }
                    Row {
                        spacing: 8
                        Accessible.role: Accessible.StaticText
                        Accessible.name: pageModel.durationText
                        Repeater {
                            model: pageModel.durationParts
                            delegate: Row {
                                id: part
                                required property var modelData
                                spacing: 4
                                Text {
                                    text: part.modelData.value
                                    color: Theme.color.onPrimaryContainer
                                    font: Theme.type.displayLargeEmphasized.font
                                }
                                Text {
                                    anchors.baseline: parent.children[0].baseline
                                    text: part.modelData.unit
                                    color: Theme.color.onPrimaryContainer
                                    font: Theme.type.titleLargeEmphasized.font
                                }
                            }
                        }
                    }
                    Row {
                        visible: pageModel.compareText !== "" && page.hasPeriod
                        spacing: 4
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: pageModel.compareDirection !== 0
                            glyph: pageModel.compareDirection > 0 ? Symbols.arrow_upward
                                                                  : Symbols.arrow_downward
                            size: 18
                            color: Qt.alpha(Theme.color.onPrimaryContainer, 0.8)
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: pageModel.compareText
                            color: Qt.alpha(Theme.color.onPrimaryContainer, 0.8)
                            font: Theme.type.bodyMedium.font
                        }
                    }
                    Text {
                        visible: !page.hasPeriod
                        text: [qsTr("这一周没有听歌"), qsTr("这个月没有听歌"),
                               qsTr("这一年没有听歌"), qsTr("还没有听歌")][pageModel.range]
                        color: Qt.alpha(Theme.color.onPrimaryContainer, 0.8)
                        font: Theme.type.bodyMedium.font
                    }
                }
                BarChart {
                    id: chart
                    x: page.wide ? headline.x + headline.width + 24 : 20
                    y: page.wide ? 24 : headline.y + headline.height + 20
                    width: parent.width - x - 28
                    height: page.wide ? 260 - 48 : 180
                    bars: pageModel.bars
                    scaleTop: pageModel.chartTop
                    grid: pageModel.grid
                    peak: pageModel.peak
                    peakText: pageModel.peakText
                    barColor: Theme.color.primary
                    textColor: Theme.color.onPrimaryContainer
                    onActivated: index => pageModel.openBar(index)
                }
            }

            // The four counts.
            Grid {
                id: counts
                visible: page.hasPeriod
                width: parent.width
                columns: page.wide ? 4 : 2
                columnSpacing: 16
                rowSpacing: 16
                Repeater {
                    model: [
                        [pageModel.plays, qsTr("次播放")],
                        [pageModel.songCount, qsTr("首歌")],
                        [pageModel.artistCount, qsTr("位歌手")],
                        [pageModel.newSongs, qsTr("首新歌")]
                    ]
                    delegate: StatTile {
                        required property var modelData
                        width: (content.width - (page.wide ? 48 : 16)) / (page.wide ? 4 : 2)
                        value: App.format.count(modelData[0])
                        label: modelData[1]
                    }
                }
            }

            // What: songs, artists, albums.
            Item {
                id: mostTitle
                visible: page.hasPeriod
                width: parent.width
                height: 40
                SectionTitle {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("听得最多")
                }
                ConnectedButtonGroup {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    exclusive: true
                    Button {
                        variant: Md.Tonal
                        size: Md.XSmall
                        checkable: true
                        checked: !pageModel.byTime
                        text: qsTr("按次数")
                        onClicked: App.settings.statsByTime = false
                    }
                    Button {
                        variant: Md.Tonal
                        size: Md.XSmall
                        checkable: true
                        checked: pageModel.byTime
                        text: qsTr("按时长")
                        onClicked: App.settings.statsByTime = true
                    }
                }
            }
            Flow {
                id: most
                visible: page.hasPeriod
                width: parent.width
                spacing: 32
                Column {
                    width: page.wide ? (parent.width - 32) * 0.58 : parent.width
                    spacing: 8
                    Item {
                        width: parent.width
                        height: 40
                        SubTitle {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("歌曲")
                        }
                        Button {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: pageModel.songs.count > 0
                            variant: Md.Text
                            text: qsTr("查看全部")
                            onClicked: page.shell.openStatsSongs(pageModel.range, pageModel.offset)
                        }
                    }
                    SongList {
                        width: parent.width
                        height: contentHeight
                        interactive: false
                        sideMargin: 0
                        bottomMargin: 0
                        model: pageModel.songs
                        rankValues: pageModel.songValues
                        onAlbumRequested: key => page.shell.openAlbum(key)
                        onArtistRequested: key => page.shell.openArtist(key)
                        onSongRequested: key => page.shell.openSong(key)
                    }
                }
                Column {
                    width: page.wide ? (parent.width - 32) * 0.42 : parent.width
                    spacing: 8
                    SubTitle {
                        height: 40
                        verticalAlignment: Text.AlignVCenter
                        visible: pageModel.artists.length > 0
                        text: qsTr("歌手")
                    }
                    Ranking {
                        rows: pageModel.artists
                        glyph: Symbols.person
                        round: true
                        onPicked: row => page.shell.openArtist(row.key)
                    }
                    SubTitle {
                        height: 40
                        verticalAlignment: Text.AlignVCenter
                        visible: pageModel.albums.length > 0
                        text: qsTr("专辑")
                    }
                    Ranking {
                        rows: pageModel.albums
                        covers: true
                        glyph: Symbols.album
                        onPicked: row => page.shell.openAlbum(row.key)
                    }
                }
            }

            // When, and from where.
            Flow {
                id: whenWhere
                visible: page.hasPeriod
                width: parent.width
                spacing: 32
                Column {
                    id: whenColumn
                    width: page.wide && pageModel.origins.length > 0
                           ? Math.max(heat.implicitWidth, (parent.width - 32) * 0.58)
                           : parent.width
                    spacing: 12
                    SectionTitle {
                        text: qsTr("什么时候听")
                    }
                    Flickable {
                        width: parent.width
                        height: heat.implicitHeight
                        contentWidth: heat.implicitWidth
                        interactive: contentWidth > width
                        clip: interactive
                        boundsBehavior: Flickable.StopAtBounds
                        HeatGrid {
                            id: heat
                            // Smaller cells before it has to scroll.
                            cell: whenColumn.width >= 24 + 24 * 20 + 23 * 2 ? 20 : 14
                            levels: pageModel.heat
                            tips: pageModel.heatTips
                        }
                    }
                    Text {
                        visible: text !== ""
                        text: pageModel.heatSummary
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }
                }
                Column {
                    visible: pageModel.origins.length > 0
                    width: page.wide ? parent.width - whenColumn.width - 32 : parent.width
                    spacing: 12
                    SectionTitle {
                        text: qsTr("从哪里听")
                    }
                    Ranking {
                        rows: pageModel.origins
                        onPicked: row => page.openOrigin(row)
                    }
                }
            }

            // How plays are counted; the service's own ranking.
            Item {
                width: parent.width
                height: 40
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - (cloudRanking.visible ? cloudRanking.width + 16 : 0)
                    text: qsTr("听满 30 秒计一次播放，时长包含所有播放")
                    wrapMode: Text.Wrap
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.bodySmall.font
                }
                Button {
                    id: cloudRanking
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: App.session.signedIn
                    variant: Md.Text
                    trailingGlyph: Symbols.chevron_right
                    text: qsTr("网易云听歌排行")
                    onClicked: page.shell.openRanking()
                }
            }
        }
    }
}
