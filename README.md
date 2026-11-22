<img align="right" src="resources/brand/zopana.svg" alt="zopana logo" width="80" height="80">

# zopana

English · [简体中文](README.zh-CN.md)

[![Windows CI status](https://github.com/perillaroc/zopana/actions/workflows/ci.yml/badge.svg?branch=main&event=push)](https://github.com/perillaroc/zopana/actions/workflows/ci.yml)
[![Windows 11 x64](https://img.shields.io/badge/Windows-11%20x64-0078D4)](#download-and-run)
[![Apache 2.0 license](https://img.shields.io/github/license/perillaroc/zopana)](LICENSE)

zopana is a local video player for Windows. Zoom in on the area under your pointer and drag the picture to see details.

[Releases](https://github.com/perillaroc/zopana/releases) · [Quick start](#download-and-run) · [User guide](docs/en/guide/playback.md)

## Download and run

The portable build runs on **Windows 11 x64** and includes the required runtime libraries.

1. When a release is available, download `zopana-windows-x64.zip` from its **Assets** on GitHub Releases.
2. Extract the entire ZIP and double-click `zopana-windows-x64\zopana.exe`. Keep the extracted folder together when moving the player.
3. Click the folder icon in the playback controls to open a local video, or drag a video into the window.

Download `zopana-windows-x64.zip.sha256` alongside the ZIP to verify the download. [Verification steps](docs/en/quick-start.md)

## First playback

On first launch, the mouse wheel zooms in or out around the pointer. Hold the left mouse button and drag to move the picture. Use the fit-to-window button to show the whole frame again.

Use the mouse mode button in the top bar to switch to normal playback. In this mode, the wheel adjusts volume and clicking the video pauses or resumes playback. [Mouse modes](docs/en/guide/zoom-and-pan.md)

## Features

- **Framing and comparison.** Zoom from 1× to 8×, rotate the picture, briefly view the full frame, or pin a reference frame to compare it with the current picture. [Zoom and pan](docs/en/guide/zoom-and-pan.md)
- **Playback controls.** Seek in three step sizes, step forward or backward one frame at a time, adjust playback speed, loop an A–B segment, and set intro and outro skip durations. [Playback and window controls](docs/en/guide/playback.md) · [Advanced playback](docs/en/guide/media.md)
- **Local playlists.** Add files in batches or import a folder, search and sort the queue, and use repeat or shuffle. Import and export local `.m3u8` playlists. [Files and playlists](docs/en/guide/playlist.md)
- **Audio and subtitles.** Switch tracks, load external subtitles or automatically match subtitle files, and adjust subtitle size and audio or subtitle delay. [Audio and subtitles](docs/en/guide/media.md)
- **Screenshots and clipboard.** Save the full video frame, save the current view with visible subtitles, or copy the current view to the clipboard. [Screenshots and settings](docs/en/guide/settings.md)
- **Playback history.** Enable playback history in settings to remember recent files and resume from saved positions. [Playback history](docs/en/guide/settings.md)

## Feedback and contributions

Report problems or suggest features on GitHub. For a bug report, include your zopana version, Windows version, and steps to reproduce it. [GitHub Issues](https://github.com/perillaroc/zopana/issues)

Pull requests for code and documentation are welcome.

## Build from source

zopana uses C++20, Qt Widgets, and libmpv. Build it on Windows x64 with Visual Studio 2022, CMake, and Ninja. [Build and run](docs/en/development/build.md) · [Packaging and release](docs/en/development/packaging.md) · [Playback validation (Chinese)](experiments/playback-controls/stage-6/README.md) · [Documentation development](docs/en/development/docs.md)

## License

zopana's own source code is licensed under the Apache License 2.0 (`Apache-2.0`). Third-party components are distributed under their respective licenses. [License](LICENSE) · [Third-party licenses and sources](packaging/notices/SOURCES.txt)
