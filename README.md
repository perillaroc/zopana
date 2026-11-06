# Zopana

阶段 0 用一个 Qt 窗口验证视频渲染环境。程序启动时在 `QOpenGLWidget` 的 OpenGL 上下文中创建 libmpv 渲染器，窗口显示深蓝绿色底色，终端输出运行库和显卡信息。

## 开发环境

| 工具 | Windows x64 构建基线 | 获取方式 |
| --- | --- | --- |
| 编译器和 SDK | Visual Studio 2022，MSVC v143（`cl` 19.44.35226 x64），Windows SDK 10.0.26100.0。安装“使用 C++ 的桌面开发”工作负载 | [Visual Studio 2022](https://visualstudio.microsoft.com/vs/older-downloads/) |
| 构建工具 | CMake 4.0.2，Ninja 1.12.1 | [CMake 4.0.2](https://github.com/Kitware/CMake/releases/tag/v4.0.2) · [Ninja 1.12.1](https://github.com/ninja-build/ninja/releases/tag/v1.12.1) |
| Qt | Qt 6.11.3 的 `msvc2022_64` kit，含 Widgets 和 OpenGLWidgets | [Qt 安装器](https://www.qt.io/development/download-qt-installer-oss) |
| 播放引擎 | libmpv 0.42.0 Windows x64 LGPL SDK，client API 2.6 | [mpv 0.42.0 发布包](https://github.com/mpv-player/mpv/releases/tag/v0.42.0) |

在已加载 VS 2022 **x64** 开发环境的 PowerShell 中执行以下命令。也可从开始菜单的 **x64 Native Tools Command Prompt for VS 2022** 运行 `powershell -NoProfile`。

## 准备 libmpv SDK

本工作区已在仓库外的 `../../deps/libmpv/0.42.0-windows-x64/` 备好 SDK，可以直接构建。独立克隆时可将 SDK 放在任意目录。所需文件为 `include/mpv/` 下的头文件、`libmpv-2.dll` 和 MSVC 导入库 `libmpv.lib`。

如需从头准备，下载 Windows x64 LGPL 版 libmpv，在下载目录执行以下命令核对 ZIP 的 SHA-256，然后解压到选定的 SDK 目录。[下载 libmpv 0.42.0 压缩包](https://github.com/mpv-player/mpv/releases/download/v0.42.0/libmpv-v0.42.0-x86_64-w64-mingw32-lgpl.zip)

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

## 构建与启动

进入本仓库根目录（含 `CMakeLists.txt` 和 `main.cpp`）。将 `$qt` 和 `$ninjaBin` 改为本机安装路径。`CMAKE_PREFIX_PATH` 应指向包含 `lib/cmake/Qt6/Qt6Config.cmake` 的 Qt kit。`$mpv` 用作 `MPV_ROOT`，独立克隆时将下面的相对路径替换为 SDK 实际目录。

```powershell
$qt = 'C:\lang\qt\6.11.3\msvc2022_64'
$ninjaBin = 'C:\lang\qt\Tools\Ninja'
$mpv = (Resolve-Path '..\..\deps\libmpv\0.42.0-windows-x64' -ErrorAction Stop).Path
$build = Join-Path $PWD ('build\stage0-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$env:CMAKE_PREFIX_PATH = $qt
$env:PATH = "$qt\bin;$ninjaBin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$qt\plugins\platforms"

cmake -S . -B $build -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release "-DMPV_ROOT:PATH=$mpv"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build $build --target Zopana
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

$expected = (Get-FileHash "$mpv\libmpv-2.dll" -Algorithm SHA256).Hash
$actual = (Get-FileHash "$build\libmpv-2.dll" -Algorithm SHA256).Hash
if ($actual -ne $expected) { throw 'libmpv DLL hash mismatch' }

& "$build\Zopana.exe" 2>&1 | Tee-Object -FilePath "$build\startup.log"
$exitCode = $LASTEXITCODE
"Zopana exit code = $exitCode"
```

构建目标使用 C++20。单配置 Ninja 将 `Zopana.exe` 输出到 `$build` 顶层，构建时自动将 SDK 中的 `libmpv-2.dll` 复制到同一目录。启动时保持上述终端环境，使 Qt 可以加载 kit 中的运行库和 Windows 平台插件。

## 窗口验收

1. 确认标题为 **Zopana**。拖动窗口边框使其变大，再用另一窗口遮挡后移开。深蓝绿色的视频区应随尺寸变化填满客户区，恢复可见后应完整重绘。
2. 关闭 Zopana 窗口，确认终端显示 `Zopana exit code = 0`。
3. 在 `$build\startup.log` 核对以下关键行。GL 三项以本机显卡和驱动输出为准。

```text
Qt runtime: 6.11.3
libmpv client API (headers): 2.6
libmpv client API (runtime): 2.6
GL vendor: <厂商>
GL renderer: <显卡或渲染器名称>
GL version: <OpenGL 版本及驱动信息>
mpv_render_context_create: 0 (success)
```
