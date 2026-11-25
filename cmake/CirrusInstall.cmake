# What `cmake --install` puts where (packaging/README.md). Packages build with
# BUILD_TESTING=OFF and CIRRUS_BUILD_GALLERY=OFF: only the app is installed.
#
# Linux (GNU layout under CMAKE_INSTALL_PREFIX):
#   bin/<CIRRUS_EXECUTABLE>
#   share/applications/com.guang233.Cirrus.desktop
#   share/metainfo/com.guang233.Cirrus.metainfo.xml
#   share/icons/hicolor/<N>x<N>/apps/com.guang233.Cirrus.png, scalable/apps/….svg
#   share/doc/cirrus/      Cirrus's license (MIT), third-party notices and license texts
# Windows: the executable and its DLLs in the prefix itself (CMAKE_INSTALL_BINDIR=.),
#   notices in licenses/.
# macOS: <CIRRUS_EXECUTABLE>.app in the prefix, notices in its Contents/Resources/doc/.
# CIRRUS_DEPLOY_QT also copies the Qt runtime and the non-system libraries (FFmpeg,
# QtKeychain, OpenSSL on Windows and macOS) next to it: deb, RPM, the Windows
# installer and the macOS dmg (macdeployqt puts them into the bundle).
# Without it (AUR, distro builds) the system's libraries are used.

set(CIRRUS_APP_ID "com.guang233.Cirrus")
option(CIRRUS_DEPLOY_QT "Install the Qt runtime and bundled libraries with the app (self-contained packages)" OFF)

install(TARGETS appCirrusMusic
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32)
    set(cirrus_doc_dir "licenses")
elseif(APPLE)
    set(cirrus_doc_dir "${CIRRUS_EXECUTABLE}.app/Contents/Resources/doc")
else()
    set(cirrus_doc_dir "${CMAKE_INSTALL_DATAROOTDIR}/doc/cirrus")
endif()
install(FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/packaging/THIRD-PARTY-NOTICES.md"
    DESTINATION "${cirrus_doc_dir}")
install(FILES
        "${PROJECT_SOURCE_DIR}/third_party/licenses/Apache-2.0.txt"
        "${PROJECT_SOURCE_DIR}/third_party/licenses/GPL-3.0-only.txt"
        "${PROJECT_SOURCE_DIR}/third_party/licenses/LGPL-2.1-or-later.txt"
        "${PROJECT_SOURCE_DIR}/third_party/licenses/LGPL-3.0-only.txt"
        "${PROJECT_SOURCE_DIR}/ui/theme/fonts/MiSans-LICENSE.txt"
    DESTINATION "${cirrus_doc_dir}/licenses")
install(FILES "${PROJECT_SOURCE_DIR}/third_party/qrcodegen/LICENSE"
    DESTINATION "${cirrus_doc_dir}/licenses" RENAME qrcodegen-MIT.txt)
install(FILES "${PROJECT_SOURCE_DIR}/third_party/api-enhanced/LICENSE"
    DESTINATION "${cirrus_doc_dir}/licenses" RENAME api-enhanced-MIT.txt)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The desktop file starts the installed executable through PATH; deb/RPM link
    # /usr/bin/cirrus to /opt/cirrus/bin/cirrus, while Arch installs into /usr.
    set(CIRRUS_DESKTOP_EXEC "${CIRRUS_EXECUTABLE}")
    string(TIMESTAMP CIRRUS_RELEASE_DATE "%Y-%m-%d" UTC)
    configure_file("${PROJECT_SOURCE_DIR}/packaging/linux/${CIRRUS_APP_ID}.desktop.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/${CIRRUS_APP_ID}.desktop" @ONLY)
    configure_file("${PROJECT_SOURCE_DIR}/packaging/linux/${CIRRUS_APP_ID}.metainfo.xml.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/${CIRRUS_APP_ID}.metainfo.xml" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${CIRRUS_APP_ID}.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${CIRRUS_APP_ID}.metainfo.xml"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")
    foreach(size IN ITEMS 16 24 32 48 64 128 256 512)
        install(FILES "${PROJECT_SOURCE_DIR}/ui/app/icons/cirrus-${size}.png"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps"
            RENAME "${CIRRUS_APP_ID}.png")
    endforeach()
    install(FILES "${PROJECT_SOURCE_DIR}/ui/app/icons/cirrus.svg"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
        RENAME "${CIRRUS_APP_ID}.svg")
endif()

if(CIRRUS_DEPLOY_QT)
    # Translations: the UI is Chinese-only and uses no Qt dialogs that need them.
    # Plugins are named by file, without lib and suffix. Cirrus only uses SQLite; the
    # other SQL drivers need client libraries nobody has.
    set(cirrus_deploy_options
        EXCLUDE_PLUGINS qsqlodbc qsqloci qsqlibase qsqlpsql qsqlmysql qsqlmimer)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Qt picks xcb as the default platform; Wayland sessions need their own. QML
        # debugging, Designer and embedded (EGLFS) plugins are not for a desktop app.
        list(APPEND cirrus_deploy_options
            INCLUDE_PLUGINS qwayland
            EXCLUDE_PLUGIN_TYPES designer qmltooling egldeviceintegrations)
    elseif(WIN32)
        # windeployqt aborts on plugins Windows does not have (qwayland), so the Linux
        # list stays out. Verbose: a failure then says where it stopped. The MSVC runtime
        # goes next to the exe (InstallRequiredSystemLibraries below), not vc_redist.exe.
        list(APPEND cirrus_deploy_options
            NO_COMPILER_RUNTIME
            EXCLUDE_PLUGIN_TYPES designer qmltooling
            DEPLOY_TOOL_OPTIONS --verbose 2)
    elseif(APPLE)
        # macdeployqt ignores the plugin lists above (build-package.sh removes the SQL drivers
        # Cirrus does not use). It copies every non-system library the app and its libraries
        # name by absolute path (FFmpeg, QtKeychain, Homebrew's OpenSSL and zstd) into
        # Contents/Frameworks. Verbose: a failure then says where it stopped.
        list(APPEND cirrus_deploy_options DEPLOY_TOOL_OPTIONS -verbose=2)
    endif()
    qt_generate_deploy_qml_app_script(
        TARGET appCirrusMusic
        OUTPUT_SCRIPT cirrus_deploy_script
        NO_TRANSLATIONS
        ${cirrus_deploy_options}
    )
    install(SCRIPT "${cirrus_deploy_script}")

    if(WIN32)
        # windeployqt copies Qt only. FFmpeg, QtKeychain, OpenSSL and zlib come from
        # the directories the packaging script names (CIRRUS_RUNTIME_DIRS).
        set(CIRRUS_RUNTIME_DIRS "" CACHE PATH "Directories with the bundled non-Qt DLLs")
        install(CODE "
            file(GET_RUNTIME_DEPENDENCIES
                EXECUTABLES \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/$<TARGET_FILE_NAME:appCirrusMusic>\"
                DIRECTORIES ${CIRRUS_RUNTIME_DIRS}
                PRE_EXCLUDE_REGEXES [[^api-ms-]] [[^ext-ms-]] [[^[Qq][Tt]6]]
                # Only the system's own directory (vcpkg's DLLs are in x64-windows/bin);
                # '.' stands for either path separator.
                POST_EXCLUDE_REGEXES \"^[A-Za-z]:.[Ww][Ii][Nn][Dd][Oo][Ww][Ss].\"
                RESOLVED_DEPENDENCIES_VAR cirrus_deps
                UNRESOLVED_DEPENDENCIES_VAR cirrus_missing)
            foreach(dep IN LISTS cirrus_deps)
                file(INSTALL \"\${dep}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
            endforeach()
            if(cirrus_missing)
                message(WARNING \"Unresolved DLLs: \${cirrus_missing}\")
            endif()")
        # The MSVC runtime next to the executable (app-local; no vc_redist step).
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
        include(InstallRequiredSystemLibraries)
    endif()
endif()
