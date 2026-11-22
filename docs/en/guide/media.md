# Audio, subtitles, and advanced playback

## Switch audio and subtitle tracks

The Audio (音频) and Subtitles (字幕) menus show the language and name of each track in the current file and mark tracks as embedded or external. Press **J** to switch audio tracks, **H** to show or hide subtitles, and **K** to switch subtitle tracks.

A manual track selection applies to the current file. For the next file, the player chooses tracks using your language preferences. [Track language preferences](./settings.md#seek-and-shortcuts)

Load an SRT, ASS, SSA, or VTT subtitle file with **Ctrl+L**, or drop it onto a loaded video. If you drop media and subtitles together, the player attaches matching subtitles to the first media file it adds. It also looks in the video folder for subtitles with the same base name or a language suffix, such as `movie.srt`, `movie.en.ass`, or `movie.zh-Hans.vtt`.

Subtitles scale with the picture and may be cropped when you zoom in. Press **R** to fit the frame to the window. The Subtitles menu offers a font scale of 0.5× to 2.0× and saves it along with subtitle visibility.

Set audio and subtitle delays in their respective menus. Both accept −3600 to 3600 seconds and default to 0. A positive value makes the audio or subtitles arrive later. These preferences apply across file changes and restarts. Each menu provides a command to reset its delay to zero.

## Adjust speed and step through frames

Over the video, press **[** or **]** to change speed in 0.25× steps between 0.25× and 3×. Press **Backspace** to return to 1×. Open **Playback → Speed** (播放 → 倍速) for speed presets and pitch correction. The defaults are 1× speed and pitch correction enabled. Both persist across file changes and restarts. The top bar shows the zoom or speed when its value differs from 1×.

Press **D** to pause and advance one frame. To go back, choose **Previous Frame** (后退一帧) in the Playback menu or on the Playback tab of the control window. If the video is playing, the player pauses first and then steps back. It stops at the first frame. To step back with a key, assign a video-area shortcut under **Settings → Shortcuts** (设置 → 快捷键).

Press **A** and **B** to set a loop at least 0.5 seconds long. Press **Ctrl+B** or seek outside the segment to clear the loop.
