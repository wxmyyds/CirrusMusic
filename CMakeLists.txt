cmake_minimum_required(VERSION 3.24)

project(CirrusMusic VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Multimedia)

qt_standard_project_setup(REQUIRES 6.10)
# Every log record knows its file, line and function, release builds too: warnings and
# errors carry src= and fn= (docs/tasks/logging-v2.md §11).
add_compile_definitions(QT_MESSAGELOGCONTEXT)

include(CTest)
add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# QML modules, the product entry (appCirrusMusic) and the gallery; see docs/ui/README.md.
add_subdirectory(ui)

option(CIRRUS_BUILD_AUDIO_POC "Build the audio engine PoC (tools/audio-poc, needs FFmpeg)" OFF)
if(CIRRUS_BUILD_AUDIO_POC)
    add_subdirectory(tools/audio-poc)
endif()
option(CIRRUS_BUILD_AUTOMIX_TOOL "Build the automix tuning tool (tools/automix)" OFF)
if(CIRRUS_BUILD_AUTOMIX_TOOL)
    add_subdirectory(tools/automix)
endif()
