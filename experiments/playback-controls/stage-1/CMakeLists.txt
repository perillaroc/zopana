add_executable(zopana_playback_preferences preferences-probe.cpp)
target_link_libraries(zopana_playback_preferences PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_playback_preferences PRIVATE /utf-8)
set_target_properties(zopana_playback_preferences PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET zopana_playback_preferences POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mpv::mpv>" "$<TARGET_FILE_DIR:zopana_playback_preferences>"
    VERBATIM
)

add_executable(zopana_playback_settings settings-probe.cpp)
target_link_libraries(zopana_playback_settings PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_playback_settings PRIVATE /utf-8)
set_target_properties(zopana_playback_settings PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET zopana_playback_settings POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mpv::mpv>" "$<TARGET_FILE_DIR:zopana_playback_settings>"
    VERBATIM
)

add_executable(zopana_playback_seek seek-probe.cpp)
target_link_libraries(zopana_playback_seek PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_playback_seek PRIVATE /utf-8)
set_target_properties(zopana_playback_seek PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET zopana_playback_seek POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mpv::mpv>" "$<TARGET_FILE_DIR:zopana_playback_seek>"
    VERBATIM
)

add_executable(zopana_playback_rotation rotation-probe.cpp)
target_link_libraries(zopana_playback_rotation PRIVATE zopana_core Qt6::Test)
target_compile_options(zopana_playback_rotation PRIVATE /utf-8)
set_target_properties(zopana_playback_rotation PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET zopana_playback_rotation POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:mpv::mpv>" "$<TARGET_FILE_DIR:zopana_playback_rotation>"
    VERBATIM
)
