# Repository Guidelines

## Project Structure & Module Organization

Zopana is a Windows x64 video player built with C++20, Qt Widgets, and libmpv. The Git checkout is `repo/zopana/` in the development workspace. Run commands from the checkout root unless stated otherwise.

- `src/main.cpp` starts the app. `src/` separates playback, video rendering, UI, playlists, settings, and capture. Register new app sources in `src/CMakeLists.txt`.
- `resources/player.qrc` registers Lucide icons. `docs/` contains the VitePress site; `packaging/` contains the Windows packager and license notices.
- `experiments/m*/` and `experiments/playback-controls/stage-*/` contain validation probes and scripts. The workspace's `test-media/` contains local fixtures.

## Build, Test, and Development Commands

Use VS 2022 x64 Developer PowerShell with CMake and Ninja. Set `$qt` and `$mpv` to the Qt 6.11.3 MSVC x64 kit and libmpv 0.42.0 SDK using the [build guide](docs/development/build.md).

```powershell
$build = 'build/dev'
cmake -S . -B $build -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qt" "-DMPV_ROOT:PATH=$mpv"
cmake --build $build --target zopana
```

- Run `& "$build/zopana.exe"` after setting the Qt runtime paths from the build guide.
- From the workspace root, `./build-portable.ps1` builds a portable ZIP and SHA-256 checksum under `build/portable-dist/`.
- In `docs/` with Node.js 22, run `npm.cmd ci`, then `npm.cmd run docs:dev` to preview or `npm.cmd run docs:build` to build the site.

## Coding Style & Naming Conventions

Use four spaces, UTF-8, and LF line endings (CRLF for `.cmd` and `.bat`). Use PascalCase types (`PlaylistModel`), camelCase methods (`openFile`), trailing underscores for class data members (`path_`), and lowercase `.cpp`/`.h` pairs (`playlistmodel.cpp`). Match adjacent code; no formatter or linter is configured.

## Testing Guidelines

Add regression cases to the relevant Qt Test probe (`*-probe.cpp`). Use fixture media and an unlocked Windows desktop for UI checks. For settings, enable `ZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE1_PROBE`, build `zopana` and `zopana_playback_settings`, then run `./experiments/playback-controls/stage-1/run.ps1 -Checks Settings` with the paths from the [stage 1 guide](experiments/playback-controls/stage-1/README.md). There is no coverage threshold. Follow the [packaging guide](docs/development/packaging.md) for clean Windows release checks.

## Commit & Pull Request Guidelines

Use Gitmoji shortcodes and imperative subjects, such as `:bug: Fix window size restoration` or `:memo: Update the build guide`. In PRs, describe the behavior change, report validation results, link relevant issues, and include screenshots for visible UI changes. Keep generated builds, SDKs, and local settings out of commits.
