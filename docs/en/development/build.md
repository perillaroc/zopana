# Build and run

zopana uses C++20, Qt Widgets, and libmpv. It builds on Windows with MSVC x64.

## Development environment

| Tool | Windows x64 build baseline | Download |
| --- | --- | --- |
| Compiler and SDK | Visual Studio 2022, MSVC v143 (`cl` 19.44.35226 x64), and Windows SDK 10.0.26100.0. Install the **Desktop development with C++** workload. | [Visual Studio 2022](https://visualstudio.microsoft.com/vs/older-downloads/) |
| Build tools | CMake 4.0.2 and Ninja 1.12.1. | [CMake 4.0.2](https://github.com/Kitware/CMake/releases/tag/v4.0.2) · [Ninja 1.12.1](https://github.com/ninja-build/ninja/releases/tag/v1.12.1) |
| Qt | Qt 6.11.3 `msvc2022_64` kit with Widgets, OpenGL, OpenGLWidgets, SVG, and Concurrent. | [Qt installer](https://www.qt.io/development/download-qt-installer-oss) |
| Playback engine | libmpv 0.42.0 Windows x64 LGPL SDK, client API 2.6. | [mpv 0.42.0 release](https://github.com/mpv-player/mpv/releases/tag/v0.42.0) |

Run these commands in PowerShell with the Visual Studio 2022 **x64** developer environment loaded. You can open **x64 Native Tools Command Prompt for VS 2022** from the Start menu and run `powershell -NoProfile`.

## Prepare the libmpv SDK

The SDK folder needs the headers under `include/mpv/`, the `libmpv-2.dll` binary, and the MSVC import library `libmpv.lib`. With a complete SDK, proceed to the build. [Build the player](#build-player)

Download the Windows x64 LGPL build. Verify the ZIP's SHA-256 in the download folder and extract it into your SDK folder. [libmpv 0.42.0 Windows x64 LGPL ZIP](https://github.com/mpv-player/mpv/releases/download/v0.42.0/libmpv-v0.42.0-x86_64-w64-mingw32-lgpl.zip)

```powershell
$zip = '.\libmpv-v0.42.0-x86_64-w64-mingw32-lgpl.zip'
$sha256 = '8e5f77430efc53ef175804ba6618527e3af0448a0dcabf04d3cc72659d1a6fce'
if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $sha256) { throw 'libmpv ZIP hash mismatch' }
```

Open the extracted folder that contains `libmpv-2.dll`. Generate the MSVC import library from the DLL's export table.

```powershell
$exports = dumpbin /nologo /exports .\libmpv-2.dll | ForEach-Object {
    if ($_ -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(mpv_\w+)\s*$') { $Matches[1] }
}
if ('mpv_render_context_create' -notin $exports) { throw 'Missing libmpv render API' }
@('LIBRARY libmpv-2.dll', 'EXPORTS') + $exports | Set-Content -Encoding ascii libmpv.def
lib.exe /nologo /machine:x64 /def:libmpv.def /out:libmpv.lib
if ($LASTEXITCODE -ne 0) { throw 'Failed to create libmpv.lib' }
```

## Build the player {#build-player}

Open the repository root that contains `CMakeLists.txt` and `src/`. Set `$qt`, `$ninjaBin`, and `$mpv` to your local paths. Use the Qt kit containing `lib/cmake/Qt6/Qt6Config.cmake` for `$qt` and the SDK folder from the previous step for `$mpv`.

```powershell
$qt = 'C:\Qt\6.11.3\msvc2022_64'
$ninjaBin = 'C:\Qt\Tools\Ninja'
$mpv = 'C:\SDKs\libmpv-0.42.0-windows-x64'
$build = Join-Path $PWD ('build\release-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$env:CMAKE_PREFIX_PATH = $qt
$env:PATH = "$qt\bin;$ninjaBin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$qt\plugins\platforms"

cmake -S . -B $build -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release "-DMPV_ROOT:PATH=$mpv"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build $build --target zopana
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

$expected = (Get-FileHash "$mpv\libmpv-2.dll" -Algorithm SHA256).Hash
$actual = (Get-FileHash "$build\libmpv-2.dll" -Algorithm SHA256).Hash
if ($actual -ne $expected) { throw 'libmpv DLL hash mismatch' }
```

The single-configuration Ninja build places `zopana.exe` at the top of `$build` and copies `libmpv-2.dll` beside it. When upgrading from an older version, use a fresh build folder to get the lowercase filename. If you reuse a build folder, remove the old EXE first so Windows creates the file with the current casing.

## Run and check the startup log

Start the player in the same PowerShell session to keep the Qt runtime and platform plugin paths active. This command saves `startup.log`.

```powershell
& "$build\zopana.exe" 2>&1 | Tee-Object -FilePath "$build\startup.log"
$exitCode = $LASTEXITCODE
"zopana exit code = $exitCode"
```

Open a local H.264 video with an AAC track to check playback. The startup log should include the following entries. The OpenGL values depend on your graphics hardware and driver.

```text
Qt runtime: 6.11.3
libmpv client API (headers): 2.6
libmpv client API (runtime): 2.6
GL vendor: <vendor>
GL renderer: <graphics hardware or renderer>
GL version: <OpenGL and driver version>
mpv_render_context_create: 0 (success)
```

`loadfile submitted` records the requested path and `FILE_LOADED entry=…` marks a loaded file. `duration` and `video display size` report its length and picture size. `audio output` reports the backend, sample rate, and channel count. Dragging the progress slider to five minutes logs `seek submitted: 300.000 s`. Check the `pause` and `volume` entries for playback and volume changes.

When a file reaches the end, changes, or fails, match the `END_FILE` event's `entry` to the file and check `reason` and `error`. Closing the window should produce `zopana exit code = 0`.
