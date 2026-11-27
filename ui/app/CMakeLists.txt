find_package(Qt6 REQUIRED COMPONENTS Network)

# The executable's file name. Development builds keep appCirrusMusic (docs, launch
# configs); packages name it cirrus (Linux) or Cirrus (Windows; macOS: Cirrus.app),
# packaging/README.md.
set(CIRRUS_EXECUTABLE "appCirrusMusic" CACHE STRING "File name of the app executable")

qt_add_executable(appCirrusMusic
    main.cpp
    app_icon.h
    gpu_preference.h
    gpu_preference.cpp
    memory.h
    memory.cpp
    single_instance.h
    single_instance.cpp
)

qt_add_qml_module(appCirrusMusic
    URI CirrusMusic
    QML_FILES
        Main.qml
        AppShell.qml
        SongCollectionPage.qml
        PageAction.qml
        KindLabel.qml
        PlaylistPage.qml
        PlaylistOrderPage.qml
        AlbumPage.qml
        ArtistPage.qml
        UserPage.qml
        PodcastPage.qml
        CommentsPage.qml
        TabState.qml
        SearchPage.qml
        DiscoverPage.qml
        DailySongsPage.qml
        LibraryPage.qml
        NewPlaylistDialog.qml
        GlyphTile.qml
        RecentPage.qml
        RankingPage.qml
        StatsPage.qml
        StatsSongsPage.qml
        SongPage.qml
        LoginDialog.qml
        AddToPlaylistDialog.qml
        ChartCard.qml
        SettingsPage.qml
        settings/SettingsGroup.qml
        settings/SettingRow.qml
        settings/SwitchSetting.qml
        settings/ChoiceSetting.qml
        settings/MenuSetting.qml
        settings/ActionSetting.qml
        settings/SliderSetting.qml
        settings/SeedSwatches.qml
        settings/CustomSeedDialog.qml
        settings/FontPickerDialog.qml
        settings/AccountSettings.qml
        settings/PlaybackSettings.qml
        settings/LyricsSettings.qml
        settings/PlayerSettings.qml
        settings/PlayerPanelSettings.qml
        settings/AppearanceSettings.qml
        settings/WindowSettings.qml
        settings/StorageSettings.qml
        settings/ShareSettings.qml
        settings/ShareTemplateDialog.qml
        settings/DiagnosticsSettings.qml
        settings/ExportLogsDialog.qml
        settings/ShortcutSettings.qml
        settings/AboutSettings.qml
        settings/UpdateDialog.qml
        HomePage.qml
        TodayCard.qml
        FmCard.qml
        LikedTile.qml
        WeekTile.qml
        HomeSection.qml
        SidebarRow.qml
        RailCovers.qml
        RailPlaylists.qml
        RailAccount.qml
        SearchField.qml
        TopBar.qml
        PanelCorner.qml
        CloseDialog.qml
        TogetherDialog.qml
        DateTile.qml
    SOURCES
        desktop.h
        desktop.cpp
    RESOURCES
        icons/cirrus-16.png
        icons/cirrus-24.png
        icons/cirrus-32.png
        icons/cirrus-48.png
        icons/cirrus-64.png
        icons/cirrus-128.png
        icons/cirrus-256.png
)

# macOS: the app bundle's Info.plist (Qt's template). The bundle identifier is the app ID
# (CIRRUS_APP_ID in cmake/CirrusInstall.cmake): the keychain entry and the preferences
# carry it.
set_target_properties(appCirrusMusic PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.guang233.Cirrus
    MACOSX_BUNDLE_BUNDLE_NAME Cirrus
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    # Keep the historical location build/<preset>/appCirrusMusic used by docs and launch configs.
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    OUTPUT_NAME "${CIRRUS_EXECUTABLE}"
)

# Shown in Settings › About.
target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_VERSION="${PROJECT_VERSION}")
# The package identity (arch, debian, fedora, windows, macos) and target architecture
# (x86_64 or arm64); both empty for development builds.
set(CIRRUS_PACKAGE "" CACHE STRING "Package format family: arch, debian, fedora, windows, macos or empty")
set(CIRRUS_PACKAGE_ARCH "" CACHE STRING "Target package architecture: x86_64, arm64 or empty")
target_compile_definitions(appCirrusMusic PRIVATE
    CIRRUS_PACKAGE="${CIRRUS_PACKAGE}"
    CIRRUS_PACKAGE_ARCH="${CIRRUS_PACKAGE_ARCH}")
# The commit, for the log's session header (docs/tasks/logging.md §6): checked at every
# build; the header only changes (and main.cpp only rebuilds) when the commit does.
set(CIRRUS_COMMIT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/cirrus_commit.h")
add_custom_target(cirrus_commit
    COMMAND ${CMAKE_COMMAND} -DSRC=${PROJECT_SOURCE_DIR} -DOUT=${CIRRUS_COMMIT_HEADER}
            -P ${PROJECT_SOURCE_DIR}/cmake/cirrus_commit.cmake
    BYPRODUCTS ${CIRRUS_COMMIT_HEADER}
    COMMENT "Checking the commit for the log header" VERBATIM)
add_dependencies(appCirrusMusic cirrus_commit)
target_include_directories(appCirrusMusic PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(appCirrusMusic PRIVATE
    cirrus_logging
    Qt6::Quick
    Qt6::Network
    cirrus_ui_themeplugin
    cirrus_ui_controlsplugin
    cirrus_ui_window
    cirrus_ui_windowplugin
    cirrus_presentation
    cirrus_presentationplugin
    cirrus_presentation_backend
    cirrus_ui_componentsplugin
    cirrus_ui_playerplugin
    # Fake data until the backend sources exist (UI phase 5).
    cirrus_ui_samplesplugin
    m3shapesplugin
)

# The tray icon (docs/10-linux-integration.md §4). QSystemTrayIcon lives in Qt Widgets
# and needs QApplication; without it closing the window always quits.
option(CIRRUS_UI_TRAY "Show a system tray icon (links Qt Widgets)" ON)
if(CIRRUS_UI_TRAY)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    target_sources(appCirrusMusic PRIVATE tray.h tray.cpp)
    target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_UI_TRAY)
    target_link_libraries(appCirrusMusic PRIVATE Qt6::Widgets)
endif()

# The system's media controls and media keys (docs/ui/pages/shell.md §7): MPRIS on
# Linux, SMTC on Windows, Now Playing on macOS. All read App.player (media_state.h).
# SMTC and Now Playing are not yet verified on their systems.
target_sources(appCirrusMusic PRIVATE media_state.h)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_sources(appCirrusMusic PRIVATE mpris.h mpris.cpp)
    target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_UI_MPRIS)
    target_link_libraries(appCirrusMusic PRIVATE Qt6::DBus)
elseif(WIN32)
    # The executable's icon and version information (cirrus.rc.in).
    set(CIRRUS_ICO "${CMAKE_CURRENT_SOURCE_DIR}/icons/cirrus.ico")
    file(TO_CMAKE_PATH "${CIRRUS_ICO}" CIRRUS_ICO)
    configure_file(cirrus.rc.in "${CMAKE_CURRENT_BINARY_DIR}/cirrus.rc" @ONLY)
    target_sources(appCirrusMusic PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cirrus.rc")
    # C++/WinRT from the Windows SDK (MSVC).
    option(CIRRUS_UI_SMTC "Windows media controls (SMTC); needs MSVC and the Windows SDK" ON)
    if(CIRRUS_UI_SMTC)
        target_sources(appCirrusMusic PRIVATE smtc.h smtc.cpp)
        set_source_files_properties(smtc.cpp PROPERTIES
            COMPILE_DEFINITIONS "NOMINMAX;WIN32_LEAN_AND_MEAN")
        target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_UI_SMTC)
        target_link_libraries(appCirrusMusic PRIVATE windowsapp runtimeobject)
    endif()
    # Previous, play/pause and next under the taskbar thumbnail (taskbar_buttons.h).
    target_sources(appCirrusMusic PRIVATE taskbar_buttons.h taskbar_buttons.cpp)
    target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_UI_TASKBAR)
    target_link_libraries(appCirrusMusic PRIVATE ole32 uuid)
elseif(APPLE)
    # The bundle's icon (Finder, Dock), made from the committed PNGs (tools/ui/make_icon.py)
    # with the system's iconutil. There is no 1024 px PNG, so no icon_512x512@2x.
    set(cirrus_iconset "${CMAKE_CURRENT_BINARY_DIR}/cirrus.iconset")
    set(cirrus_icns "${CMAKE_CURRENT_BINARY_DIR}/cirrus.icns")
    set(cirrus_iconset_commands "")
    set(cirrus_iconset_pngs "")
    foreach(pair IN ITEMS 16:16x16 32:16x16@2x 32:32x32 64:32x32@2x 128:128x128
                          256:128x128@2x 256:256x256 512:256x256@2x 512:512x512)
        string(REPLACE ":" ";" pair "${pair}")
        list(GET pair 0 size)
        list(GET pair 1 name)
        list(APPEND cirrus_iconset_commands COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/icons/cirrus-${size}.png" "${cirrus_iconset}/icon_${name}.png")
        list(APPEND cirrus_iconset_pngs "${CMAKE_CURRENT_SOURCE_DIR}/icons/cirrus-${size}.png")
    endforeach()
    list(REMOVE_DUPLICATES cirrus_iconset_pngs)
    add_custom_command(OUTPUT "${cirrus_icns}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${cirrus_iconset}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${cirrus_iconset}"
        ${cirrus_iconset_commands}
        COMMAND iconutil -c icns -o "${cirrus_icns}" "${cirrus_iconset}"
        DEPENDS ${cirrus_iconset_pngs}
        COMMENT "Making the bundle icon" VERBATIM)
    target_sources(appCirrusMusic PRIVATE "${cirrus_icns}")
    set_source_files_properties("${cirrus_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(appCirrusMusic PROPERTIES MACOSX_BUNDLE_ICON_FILE cirrus.icns)

    option(CIRRUS_UI_NOW_PLAYING "macOS Now Playing (MediaPlayer framework)" ON)
    if(CIRRUS_UI_NOW_PLAYING)
        enable_language(OBJCXX)
        target_sources(appCirrusMusic PRIVATE now_playing.h now_playing.mm)
        set_source_files_properties(now_playing.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        set_target_properties(appCirrusMusic PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
        target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_UI_NOW_PLAYING)
        target_link_libraries(appCirrusMusic PRIVATE "-framework AppKit" "-framework MediaPlayer")
    endif()
endif()

include("${PROJECT_SOURCE_DIR}/cmake/CirrusInstall.cmake")
