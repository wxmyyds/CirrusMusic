find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick)
set(QT_NO_PRIVATE_MODULE_WARNING ON)
# The diagnostic package names the GPU through the RHI (semi-public, in GuiPrivate) when
# there is one (ui/presentation/CMakeLists.txt). Found here so the executables linking
# the static modules see these targets.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # System Qt packages must retain the native window and diagnostic features.
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate WaylandClientPrivate CoreTools QmlTools)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/cirrus-qmlimportscanner.txt"
        CONTENT "$<TARGET_FILE:Qt6::qmlimportscanner>\n")
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/cirrus-qtpaths.txt"
        CONTENT "$<TARGET_FILE:Qt6::qtpaths>\n")
else()
    find_package(Qt6 QUIET COMPONENTS GuiPrivate)
endif()

# One output tree for every UI QML module so qmllint and tooling resolve imports.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/material-color-utilities"
                 "${CMAKE_BINARY_DIR}/third_party/material-color-utilities")
add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/m3shapes"
                 "${CMAKE_BINARY_DIR}/third_party/m3shapes")
add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/qrcodegen"
                 "${CMAKE_BINARY_DIR}/third_party/qrcodegen")
add_subdirectory(theme)
add_subdirectory(controls)
add_subdirectory(window)
add_subdirectory(presentation)
add_subdirectory(components)
add_subdirectory(player)
add_subdirectory(samples)
add_subdirectory(app)

# Development preview of themes, controls and pages; never part of a release package.
option(CIRRUS_BUILD_GALLERY "Build the UI gallery (appCirrusGallery)" ON)
if(CIRRUS_BUILD_GALLERY)
    add_subdirectory(gallery)
endif()
