cmake_minimum_required(VERSION 3.21)
project(zopana LANGUAGES CXX)

if(NOT MSVC OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "The libmpv SDK requires an MSVC x64 build. Use the VS 2022 x64 Developer PowerShell.")
endif()

find_package(Qt6 CONFIG QUIET COMPONENTS Widgets OpenGL OpenGLWidgets Svg)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Set CMAKE_PREFIX_PATH to a Qt 6 MSVC x64 kit containing Qt6Config.cmake "
        "and the Widgets, OpenGL, OpenGLWidgets and Svg packages under lib/cmake. "
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

add_library(zopana_core STATIC
    src/mainwindow.cpp src/mainwindow.h
    src/player.cpp src/player.h
    src/viewgeometry.h
    src/videowidget.cpp src/videowidget.h
    src/playeractions.cpp src/playeractions.h
    src/controlbar.cpp src/controlbar.h
    src/videosurface.cpp src/videosurface.h
    src/theme.cpp src/theme.h
    resources/player.qrc
)
set_target_properties(zopana_core PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(zopana_core PUBLIC cxx_std_20)
target_compile_options(zopana_core PRIVATE /utf-8)
target_compile_definitions(zopana_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(zopana_core PUBLIC src)
target_link_libraries(zopana_core PUBLIC Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg mpv::mpv)
add_executable(zopana src/main.cpp)
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
