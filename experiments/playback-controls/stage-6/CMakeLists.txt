add_executable(zopana_playback_release release-probe.cpp)
target_link_libraries(zopana_playback_release PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_playback_release PRIVATE /utf-8)
target_compile_definitions(zopana_playback_release PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(zopana_playback_release PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET zopana_playback_release POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mpv::mpv>" "$<TARGET_FILE_DIR:zopana_playback_release>"
    VERBATIM
)
