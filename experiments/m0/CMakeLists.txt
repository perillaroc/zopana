cmake_minimum_required(VERSION 3.21)
project(zopana_m0 LANGUAGES CXX)

# A separate configure keeps experiments out of the production build.
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Svg Test)
add_subdirectory(../.. zopana)
add_executable(zopana_m0 probe.cpp)
set_target_properties(zopana_m0 PROPERTIES AUTOMOC ON)
target_include_directories(zopana_m0 PRIVATE ../../src)
target_compile_features(zopana_m0 PRIVATE cxx_std_20)
target_compile_definitions(zopana_m0 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(zopana_m0 PRIVATE /utf-8)
target_link_libraries(zopana_m0 PRIVATE zopana_core Qt6::Test Psapi)
add_custom_command(TARGET zopana_m0 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:mpv::mpv> $<TARGET_FILE_DIR:zopana_m0>)
