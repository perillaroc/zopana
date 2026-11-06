# zopana

zopana 是一个 Windows 桌面视频播放器。打开本地视频后，可以在特色模式下放大和拖动画面，也可以切到常规模式，用滚轮调音量、单击播放或暂停。Qt Widgets 提供界面，libmpv 负责软件解码和音频输出。

## 使用 Windows x64 便携包

在打包输出目录运行 `Get-FileHash -Algorithm SHA256 .\zopana-windows-x64.zip`，与同目录的 `zopana-windows-x64.zip.sha256` 核对。完整解压 ZIP 后，双击 `zopana-windows-x64\zopana.exe`。Qt 插件、libmpv 和 VC++ x64 运行库随应用提供，请保留解压目录结构。

点击**打开文件**选择本地视频。可先暂停，在**特色**模式滚轮放大、按住左键拖动，再切到**常规**调节音量。点击**恢复适应窗口**回到完整画面。

## 开发环境

| 工具 | Windows x64 构建基线 | 获取方式 |
| --- | --- | --- |
| 编译器和 SDK | Visual Studio 2022，MSVC v143（`cl` 19.44.35226 x64），Windows SDK 10.0.26100.0。安装“使用 C++ 的桌面开发”工作负载 | [Visual Studio 2022](https://visualstudio.microsoft.com/vs/older-downloads/) |
| 构建工具 | CMake 4.0.2，Ninja 1.12.1 | [CMake 4.0.2](https://github.com/Kitware/CMake/releases/tag/v4.0.2) · [Ninja 1.12.1](https://github.com/ninja-build/ninja/releases/tag/v1.12.1) |
| Qt | Qt 6.11.3 的 `msvc2022_64` kit，含 Widgets、OpenGL 和 OpenGLWidgets | [Qt 安装器](https://www.qt.io/development/download-qt-installer-oss) |
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

进入本仓库根目录（含 `CMakeLists.txt` 和 `src/`）。将 `$qt` 和 `$ninjaBin` 改为本机安装路径。`CMAKE_PREFIX_PATH` 应指向包含 `lib/cmake/Qt6/Qt6Config.cmake` 的 Qt kit。`$mpv` 用作 `MPV_ROOT`，独立克隆时将下面的相对路径替换为 SDK 实际目录。

```powershell
$qt = 'C:\lang\qt\6.11.3\msvc2022_64'
$ninjaBin = 'C:\lang\qt\Tools\Ninja'
$mpv = (Resolve-Path '..\..\deps\libmpv\0.42.0-windows-x64' -ErrorAction Stop).Path
$build = Join-Path $PWD ('build\stage4-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
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

& "$build\zopana.exe" 2>&1 | Tee-Object -FilePath "$build\startup.log"
$exitCode = $LASTEXITCODE
"zopana exit code = $exitCode"
```

构建目标使用 C++20。单配置 Ninja 将 `zopana.exe` 输出到 `$build` 顶层，构建时自动将 SDK 中的 `libmpv-2.dll` 复制到同一目录。启动时保持上述终端环境，使 Qt 可以加载 kit 中的运行库和 Windows 平台插件。

从旧版本升级时，使用上例的新构建目录即可生成小写文件名。若复用原 Ninja 目录，先移除该目录中的旧 exe，再构建 `zopana`，以免 Windows 保留原文件名的大小写。

### 制作个人使用的便携包

在上述 VS x64 开发终端完成 Release 构建后，使用相同的 Qt kit 和 libmpv SDK 运行打包脚本。为每次打包选一个新的输出目录。

```powershell
$dist = Join-Path $PWD ('build\release-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
& .\packaging\package-windows.ps1 -BuildDir $build -QtRoot $qt -MpvRoot $mpv -OutputDir $dist
```

脚本用 `windeployqt` 部署 Qt，从指定 SDK 复制 libmpv，并从 VS 的 x64 可再发行目录复制 VC++ 运行库。输出目录包含 `zopana-windows-x64.zip` 和 `zopana-windows-x64.zip.sha256`。随包的 `README.txt` 说明启动与运行库要求。[打包脚本](packaging/package-windows.ps1) · [随包说明](packaging/README.txt)

当前便携包供个人使用。仓库保留第三方依赖的版本、源码和构建来源。对外发布前再选定 zopana 自身的许可证，并整理需要随包提供的第三方许可材料。[第三方依赖来源清单](packaging/notices/SOURCES.txt)

## 基础播放

点击**打开文件**或按 **Ctrl+O**，在文件选择器中选取一段带 AAC 音轨的本地 H.264 视频。打开后自动播放，标题栏显示文件名，控制条显示播放状态、视频尺寸和“当前时间 / 总时长”。

| 操作 | 使用方法 |
| --- | --- |
| 暂停、继续 | 点击**暂停**或**播放**，也可按空格。 |
| 拖动定位 | 拖动进度条的滑块，观察 `HH:MM:SS` 格式的目标时间。松手后提交一次定位。暂停时也可以定位并查看目标帧。 |
| 键盘定位 | 用 Tab 将焦点移到进度条。左右方向键每次调整 1 秒，Page Up / Page Down 每次调整 10 秒，Home / End 跳到进度条的起点或终点。 |
| 调节音量 | 拖动音量滑块，在 0% 到 100% 之间调整，左侧显示播放引擎反馈的音量。 |
| 全屏 | 点击**全屏**或按 **F11** 切换，按 **Esc** 退出全屏。 |
| 换片、重试 | 播放期间或显示“播放完毕”时，可用 **Ctrl+O** 打开下一段。损坏文件的错误原因显示在“打开失败”后，可在同一窗口重新选片。 |

## 视频区鼠标模式

首次启动时选中**特色**。用进度条左侧的**特色 / 常规**切换，窗口和全屏下都能看到当前选择。

| 视频区操作 | 特色 | 常规 |
| --- | --- | --- |
| 垂直滚轮 | 指向细节上滚放大、下滚缩小，每格约 15%，范围 1×–8×。指向黑边时取最近的画面边缘。 | 上滚增大音量、下滚减小音量，每格 5 个百分点，范围 0–100%。细刻度按比例累计。 |
| 左键单击 | 保持当前播放状态。 | 在视频区松开时切换一次播放或暂停。 |
| 按住左键拖动 | 移动画面，到边界停住。松开或失焦即结束拖动。 | 超过系统拖动距离后取消这次单击，画面保持原位。 |

切换模式时，当前手势结束，保留当时可见的构图、音量和播放状态。两种模式都可以用空格播放或暂停、拖动进度条与音量滑块，以及点击**恢复适应窗口**回到 1× 居中。调整窗口大小或切换全屏会保留倍率并尽量保留中心细节，打开新视频时从居中适窗开始。

## 运行日志

上述启动命令将本次运行的日志保存到 `$build\startup.log`。启动时可核对以下关键行。GL 三项以本机显卡和驱动输出为准。

```text
Qt runtime: 6.11.3
libmpv client API (headers): 2.6
libmpv client API (runtime): 2.6
GL vendor: <厂商>
GL renderer: <显卡或渲染器名称>
GL version: <OpenGL 版本及驱动信息>
mpv_render_context_create: 0 (success)
```

打开文件后，`loadfile submitted` 记录请求的路径，`FILE_LOADED entry=…` 表示文件已加载。`duration` 和 `video display size` 分别给出秒数和画面尺寸，`audio output` 给出音频后端、采样率和声道数。一次将滑块拖到 5 分钟的操作应产生一行 `seek submitted: 300.000 s`。暂停和音量变化可分别从 `pause`、`volume` 行核对。

自然播完、换片或文件损坏时可用 `END_FILE` 的 `entry` 对上对应文件，查看 `reason` 和 `error`。关闭窗口后，终端应显示 `zopana exit code = 0`。

## 源码布局

| 文件 | 职责 |
| --- | --- |
| `src/main.cpp` | 创建 Qt 应用和播放器，启动主窗口并管理退出顺序。 |
| `src/mainwindow.{h,cpp}` | 文件选择器、播放控件、快捷键和状态显示。 |
| `src/player.{h,cpp}` | 持有 `mpv_handle`，异步提交命令，按文件处理事件和属性反馈。 |
| `src/videowidget.{h,cpp}` | 接收滚轮和拖动输入，在 Qt 的 OpenGL 上下文中保存完整画面，并按当前取景绘制。 |
| `src/viewgeometry.h` | 用窗口逻辑像素计算适窗画幅、指针锚点、拖动边界及调整窗口后的构图。 |
