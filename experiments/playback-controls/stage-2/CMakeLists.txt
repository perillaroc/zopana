add_executable(zopana_playback_repeat repeat-probe.cpp playback-stage-cases.cpp)
target_link_libraries(zopana_playback_repeat PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_playback_repeat PRIVATE /utf-8)
set_target_properties(zopana_playback_repeat PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET zopana_playback_repeat POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mpv::mpv>" "$<TARGET_FILE_DIR:zopana_playback_repeat>"
    VERBATIM
)

add_executable(zopana_control_window control-window-probe.cpp)
target_link_libraries(zopana_control_window PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_control_window PRIVATE /utf-8)
set_target_properties(zopana_control_window PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET zopana_control_window POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mpv::mpv>" "$<TARGET_FILE_DIR:zopana_control_window>"
    VERBATIM
)
