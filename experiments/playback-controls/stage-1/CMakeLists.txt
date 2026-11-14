add_executable(zopana_playback_preferences preferences-probe.cpp)
target_link_libraries(zopana_playback_preferences PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_playback_preferences PRIVATE /utf-8)
set_target_properties(zopana_playback_preferences PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET zopana_playback_preferences POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mpv::mpv>" "$<TARGET_FILE_DIR:zopana_playback_preferences>"
    VERBATIM
)
