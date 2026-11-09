cmake_minimum_required(VERSION 3.21)
project(zopana_m0 LANGUAGES CXX)

# A separate configure keeps experiments out of the production build.
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Test)
add_subdirectory(../.. zopana)
# The production imported target has directory scope, so this standalone
# parent project declares its own reference to the exact same SDK binary.
add_library(m0_mpv SHARED IMPORTED)
set_target_properties(m0_mpv PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${MPV_ROOT}/include"
    IMPORTED_IMPLIB "${MPV_ROOT}/libmpv.lib"
    IMPORTED_LOCATION "${MPV_ROOT}/libmpv-2.dll")
add_executable(zopana_m0 probe.cpp
    ../../src/player.cpp ../../src/player.h
    ../../src/videowidget.cpp ../../src/videowidget.h
    ../../src/mainwindow.cpp ../../src/mainwindow.h)
set_target_properties(zopana_m0 PROPERTIES AUTOMOC ON)
target_include_directories(zopana_m0 PRIVATE ../../src)
target_compile_features(zopana_m0 PRIVATE cxx_std_20)
target_compile_definitions(zopana_m0 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(zopana_m0 PRIVATE /utf-8)
target_link_libraries(zopana_m0 PRIVATE Qt6::Widgets Qt6::OpenGL
    Qt6::OpenGLWidgets Qt6::Test m0_mpv Psapi)
add_custom_command(TARGET zopana_m0 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:m0_mpv> $<TARGET_FILE_DIR:zopana_m0>)
