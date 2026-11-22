# Screenshots and settings

## Save screenshots and inspect media

| Action | Result |
| --- | --- |
| **Ctrl+S** | Save the full frame in its playback orientation with pixel aspect ratio correction, before subtitle overlay and viewport cropping. |
| **Ctrl+Shift+S** | Save the current view, including the visible picture, subtitles, and black bars. |
| **I** | Open **Media Information** (媒体信息) to see details such as resolution. The same command is under **View** (视图). |
| **T** | Toggle Always on Top for the main window. |

Both screenshot types capture the picture before app notifications. PNG files are encoded in the background. By default they go in the `zopana` folder under Pictures. Filenames avoid overwriting existing files. A rapid change of file or orientation discards old captures that have yet to be saved. When a capture is saved, the notification shows its path and offers **Open Folder** (打开文件夹). Press **F5** to change the capture directory in Settings.

### Copy the current view {#copy-viewport}

Choose **Copy Current View** (复制当前取景) from the Picture (画面) menu. You can also open the control window with **F7**, select **Zoom and Pan** (缩放与平移), and click the copy icon to the right of Reference Frame (参考帧). Press **Ctrl+V** in Paint or another app that accepts images.

The copied image has the same pixel dimensions as a current-view screenshot of the same frame. It includes visible subtitles, the current crop, and any black bars. Copying writes to the clipboard. Use **Current View Screenshot** (当前取景截图) to save a PNG.

## Set seek steps, track language preferences, and shortcuts {#seek-and-shortcuts}

Press **F5**, open **View → Settings** (视图 → 设置), or choose Settings from the video context menu.

Open **Playback and History → Seek Steps** (播放与记录 → 定位步长) to set the small, medium, and large steps. Each accepts a whole number of seconds from 1 to 3600. The defaults are 5, 30, and 60 seconds. Click **Apply** (应用) or **OK** (确定) to use the values immediately and save them for the next launch. The Playback menu, bottom bar tooltips, progress bar, and **F1** shortcut help show the current values. [Playback and seeking](./playback.md#seek-controls)

Enter preferred audio and subtitle languages as comma-separated codes such as `jpn,eng,zho`. The player checks them in order when opening the next file. Leave a field empty to use the file's default track.

On the **Shortcuts** (快捷键) tab, filter by category, command, or key. Select an action, record up to two key combinations, and click **Set Binding** (设置绑定). Resolve conflicts by confirming a replacement. **Clear This Binding** (清空此项) clears the action's shortcuts. **Restore Default** (恢复此项) restores its default binding. **F11**, **Enter** over the video, **Esc**, **Tab**, and system-reserved keys are protected. When the playlist has focus, **Enter** plays the selected item by default.

## Apply or reset settings

**Apply** saves the changes and keeps the dialog open. **OK** saves and closes it. **Cancel** discards unapplied edits. If a write fails, the player shows a notice and keeps the active settings.

| Button | Result |
| --- | --- |
| **Reset This Page** (恢复本页默认) | Reset the draft for the current page. Playback and History resets seek steps, the capture directory, language preferences, history, and resume policy. Shortcuts resets default bindings. Skip Intro and Outro turns off all three switches and sets both durations to zero. Picture and Window resets rotation, fit-on-open, and the window size policy. |
| **Reset All Settings** (恢复全部设置) | After confirmation, reset playback preferences, framing, window layout, and shortcuts. Playback history, media files, and screenshots are kept. |

Click **Apply** or **OK** to apply a reset.

## Picture and window {#video-and-window}

On the **Picture and Window** (画面与窗口) page, set extra rotation, whether to fit the picture when opening a file, the window size policy, and the video scale. The default angle is 0°. During a session, later files inherit the current view, and the window follows its last regular size. After a restart, the picture starts centered at 1×.

Extra rotation is restored after a restart. Before you open media, the player uses the last regular window geometry. It applies the saved scale or fit policy when it learns the new video's effective size. [Rotation and framing](./zoom-and-pan.md#video-rotation) · [Window size and scale](./playback.md#window-and-fullscreen)

## Recent files and resume playback {#resume-playback}

Playback history starts off. Enable **Record recent files and playback position** (记录最近文件和播放进度) in Settings to save up to 50 entries. **File → Recent Files** (文件 → 最近文件) shows the latest 10. Missing files are grayed out. A file with a changed size or modification time starts from the beginning.

A resume position qualifies when it is more than 30 seconds from both the start and the end. By default, the player pauses on the first frame and asks what to do. **Resume** (继续) returns to the saved position. **Play from Beginning** (从头播放), pressing Play, or waiting 8 seconds uses the configured intro position, or 0 seconds if intro skipping is off. Seeking uses the specified position. You can also choose to always resume or always start from the beginning. Stopping preserves a saved position, and pressing Play again in the same session uses the intro setting. Finishing normally or automatically skipping the outro clears that item's resume position. [Skip intros and outros](./playback.md#skip-playback)

The player batches progress saves every five seconds and saves on pause, file change, and exit. Turning off recording keeps entries already saved. In Settings, click **Clear Recent Files and Playback Positions Now** (立即清除最近文件和进度) to erase the history immediately. Opening a file again can record a new entry.

## Preferences the player remembers {#remembered-preferences}

Speed, pitch correction, subtitle visibility, subtitle scale, and audio and subtitle delays apply across file changes and restarts. The player also saves volume, mute state, and mouse mode. [Audio tracks, subtitles, and speed](./media.md)

The player restores repeat mode and shuffle on launch. Shuffle playback history belongs to the current session. The player saves the last playlist sort field and direction and starts with an empty queue. [Repeat and shuffle](./playlist.md#playback-order) · [Playlist sorting](./playlist.md#sort-playlist)

The player also remembers the main skip switch, the intro and outro switches, and both durations. Turn off the main switch to suspend skipping while keeping the configured durations. [Skip intros and outros](./playback.md#skip-playback)

The player saves the main window's position, size, maximized state, and Always on Top preference. For the playlist it saves docked or separate state, visibility, dock width, and the separate window's position and size. Launch restores a regular or maximized window. Queue contents belong to the current session.

The player stores these preferences and applied settings separately from playback history. The portable build uses the current Windows user's configuration directory as well. [Settings and history files](../development/reference.md#settings-files)
