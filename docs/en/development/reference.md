# Source and configuration reference

## Settings and history files {#settings-files}

The application and organization identifiers are both `zopana`. Qt selects the locations for the current Windows user. The portable build uses the same locations.

| File | Location |
| --- | --- |
| Settings | `QStandardPaths::AppConfigLocation/settings.ini` |
| Playback history | `QStandardPaths::AppLocalDataLocation/history.json` |

`settings.ini` uses schema version 2. When loading earlier versions, the player uses valid existing values and falls back to defaults for missing or invalid fields. The first successful save of an older configuration writes `schemaVersion=2`. A higher existing version number and unknown fields are preserved. Valid custom bindings take priority over conflicting regular defaults. Explicitly cleared bindings are preserved.

Applying settings merges edited fields with current runtime preferences and atomically replaces the INI file. Runtime preference changes are batched and saved after about 700 ms, with a final save at normal shutdown. Zoom and pan apply to the current session. Saving the configuration removes the framing fields from older versions. A separate worker writes the history JSON. If saving fails, the player shows a notice and playback can continue. [Settings and history controls](../guide/settings.md)

The field names use the QSettings `group/key` format.

| Preference | Fields | Defaults |
| --- | --- | --- |
| Seek steps | `playback/seekSmallSeconds`, `playback/seekMediumSeconds`, `playback/seekLargeSeconds` | 5, 30, and 60. Each accepts whole seconds from 1 to 3600. |
| Skip switches | `skip/enabled`, `skip/introEnabled`, `skip/outroEnabled` | All `false`. |
| Skip durations | `skip/introSeconds`, `skip/outroSeconds` | Both `0`. Each accepts whole seconds from 0 to 86400. |
| Speed and pitch correction | `playback/speed`, `playback/pitchCorrection` | `1`, `true`. |
| Volume and mute | `audio/volume`, `audio/muted` | `100` (0–100 percent), `false`. |
| Repeat and shuffle | `playback/repeatMode`, `playback/shuffleEnabled` | `none`, `false`. Repeat accepts `none`, `one`, or `all`. |
| Last playlist sort | `playlist/sortField`, `playlist/sortDirection` | `fileName`, `ascending`. The sort criteria are saved. Queue paths, row order, and metadata are in memory. |
| Subtitle visibility and audio and subtitle delays | `tracks/subtitleVisible`, `tracks/audioDelay`, `tracks/subtitleDelay` | `true`, `0`, `0`. Delays are in seconds. |
| Subtitle scale and track languages | `tracks/subtitleScale`, `tracks/audioLanguages`, `tracks/subtitleLanguages` | `1` (0.5–2) and two empty strings. Languages use comma-separated codes. |
| Regular window geometry, maximized state, and Always on Top | `window/normalGeometry`, `window/maximized`, `window/alwaysOnTop` | Empty rectangle (positioned by the app), `false`, `false`. |
| Playlist layout and geometry | `playlist/detached`, `playlist/visible`, `playlist/dockWidth`, `playlist/normalGeometry` | `false`, `false`, 320 logical pixels, empty rectangle. |
| Rotation and fit-on-open | `video/userRotationDeg`, `video/resetViewOnOpen` | `0`, `false`. The extra angle accepts 0, 90, 180, or 270. |
| Window size based on video scale | `window/sizePolicy`, `window/lastVideoScale` | `lastGeometry`, `1`. Other policies are `videoScale` and `fitWorkArea`. Scale accepts 0.5, 1, 1.5, or 2. |
| Mouse mode | `input/standardMode` | `false` (Zoom and Pan). |
| Shortcuts | `shortcuts/<action ID>` | Use the action's default when no override exists. |
| Capture directory | `capture/directory` | The `zopana` subfolder under Windows Pictures. |
| Playback history and resume policy | `history/enabled`, `history/resumePolicy` | `false`, `0` (ask). `1` always resumes and `2` starts from the beginning. |

## Source layout

The application and validation tools link the shared `zopana_core` static library. The root `CMakeLists.txt` defines the application entry point, dependencies, and optional validation tools. `src/CMakeLists.txt` defines the core library and uses `MPV_ROOT` to locate the libmpv SDK.

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Launch the main window and player or enter the internal playlist metadata worker mode. |
| `src/ui/` | The main window, shared QAction objects, control bars, video status pages, theme, shortcut help, and media menus. |
| `src/playback/` | The libmpv lifecycle, async commands, per-file events, and track state. |
| `src/video/` | Video area input and OpenGL rendering, including fit-to-window, the pointer anchor, and panning bounds. |
| `src/playlist/` | The queue, playback order, docked and separate playlists, cancellable folder scans, sorting, and metadata probes. |
| `src/capture/` | Full-frame and current-view screenshots, background encoding, and capture version checks. |
| `src/settings/` | INI settings, shortcut editing, window state, recent files, and resume positions. |
| `experiments/` | Optional stage validation tools and reproduction instructions. |
| `packaging/` | Windows portable packaging scripts, package instructions, and third-party sources. |

Stage 1 checks for preferences, settings, and seeking are described in `experiments/playback-controls/stage-1/README.md`.

When sorting by file attributes or container metadata, `MetadataProbe` uses `QProcess` to launch the current executable and exchange JSON lines over two pipes. A sort reads one item at a time and reuses one worker process. Each container gets its own libmpv context, with audio and video output disabled, to read data during demuxing. Cancelling, a timeout, or exit terminates the worker. The next sort checks the canonical path, file size, and modification time before reusing an in-memory result.
