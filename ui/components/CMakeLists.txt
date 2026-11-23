# Business-facing building blocks of the shell and pages (docs/ui/README.md §3.1).
qt_add_library(cirrus_ui_components STATIC)

set_source_files_properties(CoverHandoff.qml PageInsets.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(cirrus_ui_components
    URI Cirrus.Components
    VERSION 1.0
    DEPENDENCIES
        QtQuick
        Cirrus.Theme
        Cirrus.Controls
        Cirrus.Presentation
        M3Shapes
    QML_FILES
        PageStack.qml
        PageScaffold.qml
        CoverArt.qml
        PlaybackClock.qml
        MiniPlayer.qml
        MiniPlayerButtonsMenu.qml
        SidePlayer.qml
        TogetherBadge.qml
        ShapedCover.qml
        PlayerTones.qml
        CoverColors.qml
        SideLyrics.qml
        SongSwap.qml
        WavySeek.qml
        MiniVolume.qml
        Skeleton.qml
        ListSkeleton.qml
        StateMessage.qml
        NoticeBar.qml
        PageState.qml
        SongList.qml
        SongTable.qml
        SongTableSkeleton.qml
        CarouselCard.qml
        CollectionCard.qml
        CollectionGrid.qml
        CollectionView.qml
        LoadMoreFooter.qml
        BarChart.qml
        HeatGrid.qml
        RankRow.qml
        StatTile.qml
        IconContainer.qml
        Reveal.qml
        CoverHandoff.qml
        PageInsets.qml
        CoverFlight.qml
)

target_link_libraries(cirrus_ui_components
    PRIVATE Qt6::Quick cirrus_ui_theme cirrus_ui_controls cirrus_presentation)
