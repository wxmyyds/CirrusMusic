include(GNUInstallDirs)
find_package(Threads REQUIRED)
set(cirrus_eac3_prefix "$ENV{CIRRUS_EAC3_FFMPEG_PREFIX}")
if(NOT IS_ABSOLUTE "${cirrus_eac3_prefix}" OR NOT EXISTS "${cirrus_eac3_prefix}/complete")
    message(FATAL_ERROR "Fedora EAC3 requires its verified native static FFmpeg build")
endif()
set(cirrus_eac3_archives)
foreach(component IN ITEMS avcodec swresample avutil)
    set(archive "${cirrus_eac3_prefix}/lib/lib${component}.a")
    if(NOT EXISTS "${archive}")
        message(FATAL_ERROR "Missing Fedora EAC3 archive: ${archive}")
    endif()
    list(APPEND cirrus_eac3_archives "${archive}")
endforeach()
set(cirrus_fedora_dir "${PROJECT_SOURCE_DIR}/packaging/linux/fedora")
add_library(cirrus_fedora_eac3 SHARED "${cirrus_fedora_dir}/eac3_bridge.cpp")
set_target_properties(cirrus_fedora_eac3 PROPERTIES
    OUTPUT_NAME cirrus_eac3 VERSION 1 SOVERSION 1
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
    AUTOMOC OFF AUTORCC OFF AUTOUIC OFF
    INSTALL_RPATH "$ORIGIN")
target_include_directories(cirrus_fedora_eac3 BEFORE PRIVATE "${cirrus_eac3_prefix}/include")
target_compile_features(cirrus_fedora_eac3 PRIVATE cxx_std_20)
target_link_libraries(cirrus_fedora_eac3 PRIVATE
    "-Wl,--start-group" ${cirrus_eac3_archives} "-Wl,--end-group" Threads::Threads m)
target_link_options(cirrus_fedora_eac3 PRIVATE
    "-Wl,--version-script=${cirrus_fedora_dir}/eac3.exports"
    "-Wl,--exclude-libs,ALL" "-Wl,-Bsymbolic" "-Wl,-z,defs" "-Wl,-z,noexecstack")
set_property(TARGET cirrus_fedora_eac3 APPEND PROPERTY LINK_DEPENDS "${cirrus_fedora_dir}/eac3.exports")
install(TARGETS cirrus_fedora_eac3 LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
target_sources(cirrus_media PRIVATE "${cirrus_fedora_dir}/fedora_eac3_codec.cpp")
target_include_directories(cirrus_media PRIVATE "${cirrus_fedora_dir}")
target_link_libraries(cirrus_media PRIVATE cirrus_fedora_eac3)
