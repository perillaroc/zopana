cmake_minimum_required(VERSION 3.21)
project(zopana LANGUAGES CXX)

if(NOT MSVC OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "The libmpv SDK requires an MSVC x64 build. Use the VS 2022 x64 Developer PowerShell.")
endif()

find_package(Qt6 CONFIG QUIET COMPONENTS Widgets OpenGL OpenGLWidgets Svg Concurrent)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Set CMAKE_PREFIX_PATH to a Qt 6 MSVC x64 kit containing Qt6Config.cmake "
        "and the Widgets, OpenGL, OpenGLWidgets, Svg and Concurrent packages under lib/cmake. "
        "${Qt6_NOT_FOUND_MESSAGE}")
endif()

set(MPV_ROOT "" CACHE PATH "Root of the Windows x64 libmpv SDK")
if(NOT MPV_ROOT)
    message(FATAL_ERROR "Set -DMPV_ROOT:PATH=<SDK directory> containing include/mpv, libmpv.lib and libmpv-2.dll.")
endif()
foreach(file IN ITEMS include/mpv/client.h include/mpv/render.h include/mpv/render_gl.h libmpv.lib libmpv-2.dll)
    if(NOT EXISTS "${MPV_ROOT}/${file}" OR IS_DIRECTORY "${MPV_ROOT}/${file}")
        message(FATAL_ERROR "Missing file: ${MPV_ROOT}/${file}. Set -DMPV_ROOT:PATH=<SDK directory>.")
    endif()
endforeach()

add_library(mpv::mpv SHARED IMPORTED GLOBAL)
set_target_properties(mpv::mpv PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${MPV_ROOT}/include"
    IMPORTED_IMPLIB "${MPV_ROOT}/libmpv.lib"
    IMPORTED_LOCATION "${MPV_ROOT}/libmpv-2.dll"
)

add_subdirectory(src)
add_executable(zopana WIN32 src/main.cpp)
target_compile_features(zopana PRIVATE cxx_std_20)
target_link_libraries(zopana PRIVATE zopana_core)

add_custom_command(TARGET zopana POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mpv::mpv>" "$<TARGET_FILE_DIR:zopana>"
    VERBATIM
)

option(ZOPANA_BUILD_M1_PROBE "Build the M1 UI validation tools" OFF)
if(ZOPANA_BUILD_M1_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/m1)
endif()

option(ZOPANA_BUILD_M2_PROBE "Build the M2 playback and input validation tools" OFF)
if(ZOPANA_BUILD_M2_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/m2)
endif()

option(ZOPANA_BUILD_M3_PROBE "Build the M3 playlist validation tools" OFF)
if(ZOPANA_BUILD_M3_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/m3)
endif()

option(ZOPANA_BUILD_M4_PROBE "Build the M4 media validation tools" OFF)
if(ZOPANA_BUILD_M4_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/m4)
endif()

option(ZOPANA_BUILD_M5_PROBE "Build settings, history and session validation tools" OFF)
if(ZOPANA_BUILD_M5_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/m5)
endif()

option(ZOPANA_BUILD_M6_PROBE "Build release acceptance probes" OFF)
if(ZOPANA_BUILD_M6_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/m6)
endif()

option(ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE1_PROBE "Build playback controls stage 1 validation tools" OFF)
if(ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE1_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/playback-controls/stage-1)
endif()

option(ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE2_PROBE "Build playback controls stage 2 validation tools" OFF)
if(ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE2_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/playback-controls/stage-2)
endif()

option(ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE3_PROBE "Build playback skip validation tools" OFF)
if(ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE3_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/playback-controls/stage-3)
endif()

option(ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE5_PROBE "Build playlist sorting validation tools" OFF)
if(ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE5_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/playback-controls/stage-5)
endif()

option(ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE6_PROBE "Build integrated playback and release validation" OFF)
if(ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE6_PROBE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(experiments/playback-controls/stage-6)
endif()
