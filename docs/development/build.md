# 构建与启动

zopana 使用 C++20、Qt Widgets 和 libmpv，在 Windows 上用 MSVC x64 构建。

## 开发环境

| 工具 | Windows x64 构建基线 | 获取方式 |
| --- | --- | --- |
| 编译器和 SDK | Visual Studio 2022，MSVC v143（`cl` 19.44.35226 x64），Windows SDK 10.0.26100.0。安装「使用 C++ 的桌面开发」工作负载。 | [Visual Studio 2022](https://visualstudio.microsoft.com/vs/older-downloads/) |
| 构建工具 | CMake 4.0.2，Ninja 1.12.1 | [CMake 4.0.2](https://github.com/Kitware/CMake/releases/tag/v4.0.2) · [Ninja 1.12.1](https://github.com/ninja-build/ninja/releases/tag/v1.12.1) |
| Qt | Qt 6.11.3 的 `msvc2022_64` kit，含 Widgets、OpenGL、OpenGLWidgets、SVG 和 Concurrent | [Qt 安装器](https://www.qt.io/development/download-qt-installer-oss) |
| 播放引擎 | libmpv 0.42.0 Windows x64 LGPL SDK，client API 2.6 | [mpv 0.42.0 发布页](https://github.com/mpv-player/mpv/releases/tag/v0.42.0) |

在已加载 VS 2022 **x64** 开发环境的 PowerShell 中执行以下命令。也可从开始菜单打开 **x64 Native Tools Command Prompt for VS 2022**，再运行 `powershell -NoProfile`。

## 准备 libmpv SDK

SDK 目录需要有 `include/mpv/` 下的头文件、`libmpv-2.dll` 和 MSVC 导入库 `libmpv.lib`。如果已有完整的 SDK，可以直接进入[构建](#build-player)。

下载 Windows x64 LGPL 版 libmpv。在下载目录核对 ZIP 的 SHA-256，然后解压到选定的 SDK 目录。[libmpv 0.42.0 Windows x64 LGPL 压缩包](https://github.com/mpv-player/mpv/releases/download/v0.42.0/libmpv-v0.42.0-x86_64-w64-mingw32-lgpl.zip)

```powershell
$zip = '.\libmpv-v0.42.0-x86_64-w64-mingw32-lgpl.zip'
$sha256 = '8e5f77430efc53ef175804ba6618527e3af0448a0dcabf04d3cc72659d1a6fce'
if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $sha256) { throw 'libmpv ZIP hash mismatch' }
```

进入解压后含 `libmpv-2.dll` 的目录，从 DLL 导出表生成 MSVC 导入库。

```powershell
$exports = dumpbin /nologo /exports .\libmpv-2.dll | ForEach-Object {
    if ($_ -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(mpv_\w+)\s*$') { $Matches[1] }
}
if ('mpv_render_context_create' -notin $exports) { throw 'Missing libmpv render API' }
@('LIBRARY libmpv-2.dll', 'EXPORTS') + $exports | Set-Content -Encoding ascii libmpv.def
lib.exe /nologo /machine:x64 /def:libmpv.def /out:libmpv.lib
if ($LASTEXITCODE -ne 0) { throw 'Failed to create libmpv.lib' }
```

## 构建 {#build-player}

进入源码仓库根目录（含 `CMakeLists.txt` 和 `src/`）。将 `$qt`、`$ninjaBin` 和 `$mpv` 改为本机路径。`$qt` 应指向包含 `lib/cmake/Qt6/Qt6Config.cmake` 的 Qt kit，`$mpv` 指向上一步的 SDK 目录。

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

单配置 Ninja 将 `zopana.exe` 输出到 `$build` 顶层，并把 SDK 中的 `libmpv-2.dll` 复制到同一目录。从旧版本升级时，使用新的构建目录可以生成小写的 `zopana.exe`。复用旧目录时，先移除其中的旧 EXE 再构建，以免 Windows 保留旧文件名的大小写。

## 启动与运行日志

在同一个 PowerShell 中启动，保持 Qt 的运行库和平台插件路径生效。下列命令同时保存 `startup.log`。

```powershell
& "$build\zopana.exe" 2>&1 | Tee-Object -FilePath "$build\startup.log"
$exitCode = $LASTEXITCODE
"zopana exit code = $exitCode"
```

可打开一段带 AAC 音轨的本地 H.264 视频核对播放。启动日志应包含以下信息。OpenGL 的三项信息以本机显卡和驱动为准。

```text
Qt runtime: 6.11.3
libmpv client API (headers): 2.6
libmpv client API (runtime): 2.6
GL vendor: <厂商>
GL renderer: <显卡或渲染器名称>
GL version: <OpenGL 版本及驱动信息>
mpv_render_context_create: 0 (success)
```

`loadfile submitted` 记录请求路径，`FILE_LOADED entry=…` 表示文件已加载。`duration` 和 `video display size` 分别给出时长和画面尺寸，`audio output` 给出音频后端、采样率和声道数。拖动滑块到 5 分钟时，日志会记录 `seek submitted: 300.000 s`。可从 `pause` 和 `volume` 行核对暂停与音量变化。

自然播完、换片或文件损坏时，可用 `END_FILE` 的 `entry` 对应文件，查看 `reason` 和 `error`。关闭窗口后，终端应显示 `zopana exit code = 0`。
