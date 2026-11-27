# Presentation layer (docs/ui/presentation.md): ViewModels, list models, data source
# interfaces, fakes and the cover image provider. Never links the backend; the
# backend sources will live in a separate cirrus_presentation_backend target.
find_package(Qt6 REQUIRED COMPONENTS Network)

qt_add_library(cirrus_presentation STATIC)

qt_add_qml_module(cirrus_presentation
    URI Cirrus.Presentation
    VERSION 1.0
    DEPENDENCIES
        QtQuick
    SOURCES
        load_state.h
        request.h
        collection.h
        types.h
        playback.h
        account.h
        song_item.h
        sources.h
        app.h
        app.cpp
        app_settings.h
        app_settings.cpp
        shortcut_map.h
        shortcut_map.cpp
        formatter.h
        formatter.cpp
        network_status.h
        network_status.cpp
        update_check.h
        update_check.cpp
        together.h
        together.cpp
        # Not share.h: MSVC's C runtime has a <share.h> that its iostreams include.
        sharing.h
        sharing.cpp
        share_text.h
        share_text.cpp
        app_link.h
        app_link.cpp
        update_release.h
        update_release.cpp
        cover_provider.h
        cover_provider.cpp
        loadable_model.h
        loadable_model.cpp
        song_list_model.h
        song_list_model.cpp
        sorted_songs_model.h
        sorted_songs_model.cpp
        playlist_page_model.h
        playlist_page_model.cpp
        playlist_order_model.h
        playlist_order_model.cpp
        collection_list_model.h
        collection_list_model.cpp
        album_page_model.h
        album_page_model.cpp
        artist_page_model.h
        artist_page_model.cpp
        song_page_model.h
        song_page_model.cpp
        user_page_model.h
        user_page_model.cpp
        podcast_page_model.h
        podcast_page_model.cpp
        library_models.h
        library_models.cpp
        discover_models.h
        discover_models.cpp
        cache_status.h
        cache_status.cpp
        stats_types.h
        stats_model.h
        stats_model.cpp
        diagnostics.h
        diagnostics.cpp
        diagnostic_report.h
        diagnostic_report.cpp
        notice.h
        comments_model.h
        comments_model.cpp
        paged_model.h
        paged_model.cpp
        search_results.h
        search_results.cpp
        search_model.h
        search_model.cpp
        home_page_model.h
        home_page_model.cpp
        player.h
        player.cpp
        session.h
        session.cpp
        library.h
        library.cpp
        lyrics.h
        lyrics.cpp
        ruby.h
        ruby.cpp
        song_detail_loader.h
        song_detail_loader.cpp
        qr_provider.h
        qr_provider.cpp
        fake_controls.h
        fake_controls.cpp
        fake_sources.h
        fake_sources.cpp
)

target_compile_features(cirrus_presentation PUBLIC cxx_std_20)
# main.cpp installs the sources (app.h, fake_sources.h).
target_include_directories(cirrus_presentation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_presentation PUBLIC Qt6::Quick PRIVATE cirrus_logging Qt6::Network
    Qt6::Multimedia) # diagnostic_report.cpp: audio outputs
if(TARGET Qt6::GuiPrivate) # diagnostic_report.cpp: the GPU through the RHI
    target_link_libraries(cirrus_presentation PRIVATE Qt6::GuiPrivate)
    target_compile_definitions(cirrus_presentation PRIVATE CIRRUS_HAVE_RHI)
endif()

# The backend sources: a separate target, so this one never links the backend.
add_subdirectory(backend)
