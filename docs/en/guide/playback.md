# Playback and windows

Playback starts automatically when you open a file, and the top bar shows its name. The first row of the bottom bar contains the progress and volume controls. From left to right, the second row shows play or pause, stop, skip back, skip forward, open file, and the time. Speed, the control window, and the playlist are on the right. The volume slider shrinks when the video area is narrow. You can still adjust playback speed in the control window or the Playback (播放) menu.

## Playback and seeking {#seek-controls}

| Action | How to use it |
| --- | --- |
| Pause or resume | Click **Pause** or **Play**, or press **Space** over the video. |
| Stop or restart | Click **Stop** or press **S** over the video. To restart the loaded file, press **Space**. Playback starts at the configured intro position. |
| Drag the progress slider | Drag to preview the target time as `HH:MM:SS` and release to seek. You can preview the target frame while paused. |
| Seek by a small, medium, or large step | Use **←/→** for 5 seconds by default, **Shift+←/→** for 30 seconds, or **Ctrl+←/→** for 60 seconds. These keys work over the video or when the progress slider has focus. |
| Jump to the beginning, end, or a specific time | Press **Home** for the beginning or **End** for the last seekable position. Press **Ctrl+G** to open the time dialog. |
| **PageUp / PageDown** | With the progress slider focused, seek 10 seconds forward or backward, respectively. Over the video, go to the previous or next playlist item. |
| Back and forward buttons | Left-click to seek by a small step. Right-click to play an adjacent item when available. |
| Volume and mute | Drag the volume slider or press **↑/↓** over the video to adjust volume from 0% to 100%. Press **M** or click the speaker to mute or unmute. Changing volume unmutes playback. Volume controls are disabled for files without an audio track. |
| Open the next file or retry | Press **Ctrl+O** to choose another file during playback or after it finishes. If a file is damaged, read the error and choose another file in the same window. |

Hold a seek key to repeat the action within the seekable range. Seeking while paused leaves the player paused. The progress slider and seek commands become available when the file has loaded and a seekable range is available.

Press **F5** to change the step sizes in Settings (设置) and **F1** to see the current steps and shortcuts. Use **Tab** and **Shift+Tab** to move focus between controls. With the volume slider focused, the arrow keys adjust volume. With a button focused, **Space** activates it. [Seek and shortcut settings](./settings.md#seek-and-shortcuts)

## Chapters and undo seek {#chapters-and-undo}

For files with chapters, open **Playback → Chapters** (播放 → 章节) to see the chapter names and start times. The current chapter is checked. Select a chapter from the menu or click its triangle below the progress slider. Hover over the marker to see the precise time and name. Previous Chapter (上一章) and Next Chapter (下一章) are available when the corresponding chapter exists.

Choose **Undo Last Seek** (撤销上次定位) from the Playback menu to return to the position before the last successful manual seek in the current file. This works during playback and while paused. Dragging the slider once or holding a seek key records a single starting position. Cancelled seeks, failed seeks, and seeks that end at the starting position leave the saved position available. Undoing the seek, opening another item, or stopping clears it.

The **Chapters and Undo** menu (章节与撤销) on the control window's Playback tab offers the same commands. You can jump to a chapter there and undo the seek in the main window. Manually seeking outside an A–B loop clears it. Seeking into the outro lets you continue watching that part.

## Control window {#control-window}

Click **Control Window** (控制窗口) next to the playlist button in the bottom bar or open **View → Control Window** (视图 → 控制窗口). The default shortcut is **F7** and can be changed in Settings. You can move the window separately and open it in full screen. It hides when you minimize the main window and reappears when you restore it. Close it to return to the main window.

Use the control window's **Playback** (播放) tab to seek, step through frames, and set loops in either mouse mode. Hover over an icon to see its full action name.

1. Use the play or pause icon in the Seek (定位) section to pause. The four seek buttons are large step back, small step back, small step forward, and large step forward. The drop-down arrows on the two inner buttons offer medium steps. Step sizes follow your settings.
2. Use **−** or **+** to change speed in 0.25× steps from 0.25× to 3×. Click the current speed between those buttons to return to 1×. The bottom bar and the Playback menu show the same speed.
3. After seeking, click **A**. Seek forward at least 0.5 seconds, wait for the frame to settle, and click **B**. Each button shows its timestamp to the millisecond. Click Play to start the loop and **×** to clear it. Setting A again clears B. If you click B before setting A, the player sets A and prompts you to continue.

Seekable audio-only files support play, seek, speed, and A–B controls. [Zoom and pan with the control window](./zoom-and-pan.md#control-window-framing)

## Skip intros and outros {#skip-playback}

Open **Playback → Skip Intro and Outro** (播放 → 跳略播放), turn on the main toggle, and select **Skip Intro** (跳过片头) or **Skip Outro** (跳过片尾). The submenu shows each configured duration. Over the video, **Shift+'** toggles the feature and **'** opens its settings. These controls also work with audio-only files. The progress bar displays the full duration.

Set each skip to a whole number of seconds from 0 to 86400. Use 0 to play that segment. A newly opened or restarted file begins at the configured intro position. When playback reaches the outro boundary, the next item follows the repeat and shuffle settings. Sequential order applies when shuffle is off. The player plays very short files in full and shows a notice once. If duration becomes available late, the player can use it with the current playback position to detect the outro. Changes to the durations and individual switches apply the next time you open or restart a file. Turning off the main switch takes effect immediately. [Repeat and shuffle](./playlist.md#playback-order)

Select **Resume** (继续) to start from a valid saved position. If it falls in the outro, you can watch the rest of that segment. Press **Home** to revisit the intro, or seek into the outro to watch it. Seek back into the main content to re-enable automatic outro skipping. The player suspends outro detection during pause, frame stepping, progress slider preview, and active A–B loops. Clearing an A–B loop in the outro lets the current item continue. [Resume settings](./settings.md#resume-playback) · [A–B looping](./media.md)

## Move the window and switch full screen {#window-and-fullscreen}

Choose 50%, 100%, 150%, 200%, or **Fit to Work Area** (适合工作区) under **View → Window Size** (视图 → 窗口大小) or in the video context menu. During playback, press **1–5** or use the number pad with Num Lock on. At 100%, the picture uses the same number of physical pixels as the effective video size. Window sizing accounts for controls, borders, a docked playlist, the screen work area, and the minimum window size. If the requested scale is limited, the player shows the requested and actual values.

Selecting a scale in full screen or while maximized returns to a regular window and applies that scale. The policy is used for later videos. Drag a window edge to go back to the last regular window size policy. Sizing uses the full video and carries over the current zoom, point of interest, and playback state. The scale commands are available for video files. [Window size preferences](./settings.md#video-and-window)

The left side of the top bar opens the main menu. Full screen, minimize, maximize or restore, and close are on the right. Drag an empty part of the top bar to move the window or double-click that area to maximize or restore it.

Double-click the picture, press **F11**, or press **Enter** over the video to toggle full screen. The number pad Enter key also works. In full screen, the top and bottom bars overlay the picture and hide after 2.5 seconds of inactivity. Move the mouse to the corresponding edge or use the keyboard to reveal them.

**Esc** first cancels a drag or closes an open menu or playlist overlay. Press it in full screen with those controls closed to return to the previous window state.
