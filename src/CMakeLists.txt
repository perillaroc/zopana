add_library(zopana_core STATIC
    ui/mainwindow.cpp ui/mainwindow.h ui/windowsizing.h
    playback/player.cpp playback/player.h playback/skippolicy.h
    video/viewgeometry.h
    video/videowidget.cpp video/videowidget.h
    ui/playeractions.cpp ui/playeractions.h
    ui/controlbar.cpp ui/controlbar.h
    ui/videosurface.cpp ui/videosurface.h
    ui/theme.cpp ui/theme.h
    ui/playbackdialogs.cpp ui/playbackdialogs.h
    playlist/playlistmodel.cpp playlist/playlistmodel.h
    playlist/playlistcontroller.cpp playlist/playlistcontroller.h playlist/repeatmode.h
    playlist/playlistpanel.cpp playlist/playlistpanel.h
    playlist/playlisthost.cpp playlist/playlisthost.h
    playlist/folderscanner.cpp playlist/folderscanner.h
    playback/mediastate.h
    ui/mediacontrols.cpp ui/mediacontrols.h
    capture/captureservice.cpp capture/captureservice.h
    settings/settingsstore.cpp settings/settingsstore.h
    settings/historystore.cpp settings/historystore.h
    settings/sessioncontroller.cpp settings/sessioncontroller.h
    settings/settingsdialog.cpp settings/settingsdialog.h
    ../resources/player.qrc
)
set_target_properties(zopana_core PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(zopana_core PUBLIC cxx_std_20)
target_compile_options(zopana_core PRIVATE /utf-8)
target_compile_definitions(zopana_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(zopana_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/playback" "${CMAKE_CURRENT_SOURCE_DIR}/video"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui" "${CMAKE_CURRENT_SOURCE_DIR}/playlist" "${CMAKE_CURRENT_SOURCE_DIR}/capture"
    "${CMAKE_CURRENT_SOURCE_DIR}/settings"
)
target_link_libraries(zopana_core PUBLIC Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg Qt6::Concurrent mpv::mpv)
