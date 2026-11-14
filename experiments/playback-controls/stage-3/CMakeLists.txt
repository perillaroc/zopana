add_executable(zopana_playback_skip skip-probe.cpp)
target_link_libraries(zopana_playback_skip PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_playback_skip PRIVATE /utf-8)
set_target_properties(zopana_playback_skip PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET zopana_playback_skip POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mpv::mpv>" "$<TARGET_FILE_DIR:zopana_playback_skip>"
    VERBATIM
)
