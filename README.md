# zopana

zopana 是一个 Windows 桌面视频播放器。打开本地视频后，可以在缩放拖动模式下放大和拖动画面，也可以切到常规播放模式，用滚轮调音量、单击播放或暂停。Qt Widgets 提供界面，libmpv 负责软件解码和音频输出。

[开始使用](#开始使用) · [用法](#用法) · [开发](#开发) · [参考](#参考)

## 开始使用

1. 在便携包所在目录运行 `Get-FileHash -Algorithm SHA256 .\zopana-windows-x64.zip`，与同目录的 `zopana-windows-x64.zip.sha256` 核对。
2. 完整解压 ZIP，双击 `zopana-windows-x64\zopana.exe`。Qt 插件、libmpv 和 VC++ x64 运行库随应用提供，请保留解压目录结构。
3. 点击**打开文件**或按 **Ctrl+O** 选择本地视频。可先暂停，在**缩放拖动**模式滚轮放大、按住左键拖动，再切到**常规播放**调节音量。点击**恢复适窗**回到完整画面。

从源码开始可参阅[构建与启动](#构建与启动)和[制作个人使用的便携包](#制作个人使用的便携包)。

## 用法

### 基础播放

打开文件后自动播放，顶栏显示文件名，底栏显示当前时间和总时长。底栏第一行是播放进度和音量，第二行从左到右是播放／暂停、停止、后退、前进、打开文件和时间，播放列表在最右侧。窄窗口会缩短音量滑块，保留常用按钮。

| 操作 | 使用方法 |
| --- | --- |
| 暂停、继续 | 点击**暂停**或**播放**，也可在视频区按 Space。 |
| 停止、重播 | 点击停止或在视频区按 S。停止会保留文件，再按 Space 从头播放。 |
| 拖动定位 | 拖动进度条可预览 `HH:MM:SS` 格式的目标时间。松手后提交一次定位，暂停时也可定位并查看目标帧。 |
| 视频区定位 | ←/→ 跳转 5 秒，Shift+←/→ 跳转 30 秒，Ctrl+←/→ 跳转 60 秒。Home/End 跳到开头或结尾，Ctrl+G 打开时间输入。 |
| 进度条定位 | 按 Tab 将焦点移到进度条。←/→ 调整 1 秒，PageUp/PageDown 调整 10 秒，Home/End 跳到起点或终点。 |
| 后退、前进与切换列表项 | 左键点击后退或前进按钮可在当前文件内跳转 5 秒。右键点击可切到上一项或下一项。 |
| 调节音量、静音 | 拖动音量滑块或在视频区按 ↑/↓，范围 0–100%。按 M 或点击扬声器切换静音。静音时调音量会取消静音。无音轨时音量控件禁用。 |
| 换片、重试 | 播放期间或显示「播放完毕」时，可用 Ctrl+O 打开下一段。损坏文件的原因显示在错误提示中，可在同一窗口重新选片。 |

按 F1 查看当前快捷键。Tab 和 Shift+Tab 可在控件间导航，滑块上的方向键用于调节滑块，按钮上的 Space 用于激活该按钮。

### 窗口与全屏

顶部工具条左侧打开主菜单，右侧提供全屏、最小化、最大化／还原和关闭。拖动工具条空白处移动窗口，双击切换最大化。双击画面、按 F11 或在视频区按 Enter（含数字小键盘）可切换全屏。

全屏顶栏和底栏覆盖在画面上，闲置 2.5 秒后隐藏。将鼠标移到对应边缘或用键盘操作可唤出控件。Esc 先取消拖动或关闭菜单、播放列表覆盖面板，再退出全屏并恢复进入前的窗口状态。

### 视频区鼠标模式

首次启动时选中**缩放拖动**。用顶栏的**缩放拖动／常规播放**按钮或在视频区按 V 切换。

| 视频区操作 | 缩放拖动 | 常规播放 |
| --- | --- | --- |
| 垂直滚轮 | 指向细节上滚放大、下滚缩小，每格约 15%，范围 1×–8×。指向黑边时取最近的画面边缘。 | 上滚增大音量、下滚减小音量，每格 5 个百分点，范围 0–100%。细刻度按比例累计。 |
| 左键单击 | 保持当前播放状态。 | 松开后等待系统双击间隔，确认是单击后切换一次播放或暂停。 |
| 按住左键拖动 | 移动画面，到边界停住。松手或失焦即结束拖动。 | 超过系统拖动距离后取消这次单击，画面保持原位。 |
| 双击 | 切换全屏。 | 切换全屏，保留播放状态。 |
| Ctrl+滚轮 | 临时以指针为锚点缩放。 | 临时以指针为锚点缩放，保留常规播放模式。 |

在视频区按 +/−（含数字小键盘）可按中心缩放。点击**恢复适窗**或在视频区按 R 回到 1× 居中。切换模式时，当前手势结束，保留当时可见的构图、音量和播放状态。调整窗口大小或切换全屏会保留倍率并尽量保留中心细节，打开新视频时从居中适窗开始。

### 打开文件与播放列表

打开文件支持多选，替换列表并播放第一项。Ctrl+Shift+O 添加文件，正在播放时保持当前内容。拖入视频区默认替换，按 Ctrl 追加。拖入列表时追加或按插入线放置。「添加文件夹」在后台扫描一层目录，按自然文件名排序并去重，可取消。列表项开始播放时读取时长。

按 P 或点击列表按钮切换播放列表显隐。列表标题的「独立窗口」可将它移到浮动窗口，浮动列表可单独移动和调整大小，与主窗口共用一个任务栏项目。主窗口最小化和恢复时，已展开的浮动列表随之隐藏和显示。点击浮动列表的关闭按钮或在列表中按 Ctrl+W 可收起列表。宽窗口停靠时可拖动分隔条，窄窗口与全屏在视频区使用覆盖面板。

列表中按 Enter 或双击播放所选，Delete 移除，Ctrl+↑/↓ 重排。只选择、添加或重排会保留当前播放项和播放状态。移除当前项会停止并选中相邻项，可手动播放。视频区 PageUp/PageDown 切上一项或下一项。播放菜单可选择顺序、单曲循环、列表循环或随机。随机上一项按播放记录返回，自动续播连续坏文件有遍历上限。

### 音轨、字幕与进阶播放

音频和字幕菜单按当前文件的实际轨道显示语言、名称和内封或外挂信息。J 切音轨，H 显隐字幕，K 切字幕轨，Ctrl+L 手动加载 SRT/ASS/SSA/VTT。也可将字幕拖入已加载的媒体，混合拖入时仅关联与第一个媒体同名的字幕。同目录自动匹配同基名或语言后缀字幕，例如 `movie.srt`、`movie.en.ass`、`movie.zh-Hans.vtt`。

字幕随画面缩放，放大时可能裁切，可按 R 恢复适窗。字幕菜单提供 0.5–2.0× 字体大小和字幕延迟，音频菜单提供音频延迟。正延迟表示较晚，音频和字幕延迟在换片时归零。

视频区按 `[` 或 `]` 每次调整 0.25× 倍速，Backspace 恢复 1×。播放菜单提供 0.5/0.75/1/1.25/1.5/2/3× 及音调保持。倍速在本次会话换片后延续，重启回到 1×。缩放或倍速偏离 1× 时，顶栏显示对应数值。

D 暂停并向前一帧，A/B 设置至少相隔 0.5 秒的区间循环，Ctrl+B 清除。定位到区间外也会清除循环。

### 截图、媒体信息与置顶

Ctrl+S 保存无字幕的原始画面，Ctrl+Shift+S 保存包含字幕和黑边的当前取景，排除应用 OSD。PNG 在后台写入系统图片目录的 `zopana` 子目录，文件名自动避开已有文件。保存成功后显示路径及「打开文件夹」。按 F5 打开设置可更改截图目录。

按 I 或选择「视图 → 媒体信息」查看分辨率等实际信息。按 T 切换主窗口置顶。

### 设置与播放记忆

按 F5 或从主菜单选择「视图 → 设置」，可调整截图目录、音轨和字幕语言偏好、历史开关和断点策略。语言代码按逗号分隔，例如 `jpn,eng,zho`，在下次打开媒体时生效。快捷键页支持分类过滤、每项录制两个单组合键、冲突时确认替换，以及单项或全部恢复。应用或确定后更新快捷键、帮助和提示，取消会丢弃未应用的修改。F11/Enter 全屏、Esc、Tab 和系统保留键保持保护。

最近记录最多 50 项，文件菜单显示 10 项，启动空态显示 5 项。文件不存在时保留禁用入口。文件大小或修改时间变化时，从头播放。播放超过 30 秒且距结尾超过 30 秒时，默认先暂停首帧并提示继续或从头。8 秒内可作出选择、主动播放或定位，超时则从头播放。也可设置总是继续或始终从头。停止保留已存断点，本会话按播放则从 0 重播。自然播放完毕会清除该断点。

播放进度每 5 秒合并保存，暂停、切片和退出时补存。关闭记录会停止新增和更新，保留旧记录。「清除最近文件和进度」立即生效，取消设置窗口后仍然有效。再次主动打开文件时，可重新记录该文件。

主窗口的普通位置和大小及最大化状态，以及列表的位置、停靠或浮动形态、显隐和宽度会自动保存。启动时恢复普通或最大化状态，队列内容按本次会话管理。音量、静音、鼠标模式和字幕字体比例会记住。便携包也使用 Windows 用户目录保存设置和历史，具体路径见[配置与历史文件](#配置与历史文件)。

## 开发

### 开发环境

| 工具 | Windows x64 构建基线 | 获取方式 |
| --- | --- | --- |
| 编译器和 SDK | Visual Studio 2022，MSVC v143（`cl` 19.44.35226 x64），Windows SDK 10.0.26100.0。安装“使用 C++ 的桌面开发”工作负载 | [Visual Studio 2022](https://visualstudio.microsoft.com/vs/older-downloads/) |
| 构建工具 | CMake 4.0.2，Ninja 1.12.1 | [CMake 4.0.2](https://github.com/Kitware/CMake/releases/tag/v4.0.2) · [Ninja 1.12.1](https://github.com/ninja-build/ninja/releases/tag/v1.12.1) |
| Qt | Qt 6.11.3 的 `msvc2022_64` kit，含 Widgets、OpenGL、OpenGLWidgets、SVG 和 Concurrent | [Qt 安装器](https://www.qt.io/development/download-qt-installer-oss) |
| 播放引擎 | libmpv 0.42.0 Windows x64 LGPL SDK，client API 2.6 | [mpv 0.42.0 发布包](https://github.com/mpv-player/mpv/releases/tag/v0.42.0) |

在已加载 VS 2022 **x64** 开发环境的 PowerShell 中执行以下命令。也可从开始菜单的 **x64 Native Tools Command Prompt for VS 2022** 运行 `powershell -NoProfile`。

### 准备 libmpv SDK

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

### 构建与启动

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

构建目标使用 C++20。单配置 Ninja 将 `zopana.exe` 输出到 `$build` 顶层，构建时自动将 SDK 中的 `libmpv-2.dll` 复制到同一目录。启动时保持上述终端环境，使 Qt 可以加载 kit 中的运行库和 Windows 平台插件。输出示例见[运行日志](#运行日志)。

从旧版本升级时，使用上例的新构建目录即可生成小写文件名。若复用原 Ninja 目录，先移除该目录中的旧 exe，再构建 `zopana`，以免 Windows 保留原文件名的大小写。

### 制作个人使用的便携包

在上述 VS x64 开发终端完成 Release 构建后，使用相同的 Qt kit 和 libmpv SDK 运行打包脚本。为每次打包选一个新的输出目录。

```powershell
$dist = Join-Path $PWD ('build\release-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
& .\packaging\package-windows.ps1 -BuildDir $build -QtRoot $qt -MpvRoot $mpv -OutputDir $dist
```

脚本用 `windeployqt` 部署 Qt，从指定 SDK 复制 libmpv，并从 VS 的 x64 可再发行目录复制 VC++ 运行库。输出目录包含 `zopana-windows-x64.zip` 和 `zopana-windows-x64.zip.sha256`。随包的 `README.txt` 说明启动与运行库要求。[打包脚本](packaging/package-windows.ps1) · [随包说明](packaging/README.txt)

### 发布验收

发布验收可在独立 Windows x64 上运行 M6 脚本。它会校验 SHA-256，将便携包解压到含中文和空格的目录，验证进程加载的 Qt 和 libmpv 来自随包文件，并记录播放、输入、独立列表、截图和设置的手动结果。[M6 复现说明](experiments/m6/README.md)

## 参考

### 配置与历史文件

应用和组织标识固定为 `zopana`。设置位于 `QStandardPaths::AppConfigLocation/settings.ini`，历史位于 `AppLocalDataLocation/history.json`，与启动工作目录无关。设置保留有效字段和未知字段，损坏值回退默认。历史 JSON 在工作线程原子替换。设置或历史保存失败时会提示，当前播放仍可继续。

### 运行日志

[构建与启动](#构建与启动)中的命令将本次运行的日志保存到 `$build\startup.log`。可用一段带 AAC 音轨的本地 H.264 视频核对播放。GL 三项以本机显卡和驱动输出为准。

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

### 源码布局

| 文件 | 职责 |
| --- | --- |
| `src/main.cpp` | 创建 Qt 应用和播放器，启动主窗口并管理退出顺序。 |
| `src/ui/mainwindow.{h,cpp}` | 组装界面、文件选择和播放状态呈现。 |
| `src/ui/playeractions.{h,cpp}` | 共享 QAction、快捷键作用域、可用状态与图标。 |
| `src/ui/controlbar.{h,cpp}` | 自适应控制栏、进度预览和音量入口。 |
| `src/ui/videosurface.{h,cpp}` | 视频容器与空态、加载、错误、结束等状态页。 |
| `src/ui/theme.{h,cpp}` | 深色调色板、系统高对比度及 SVG 图标绘制。 |
| `src/ui/playbackdialogs.{h,cpp}` | 毫秒时间输入、校验和从共享动作表生成快捷键帮助。 |
| `src/ui/mediacontrols.{h,cpp}` | 音轨/字幕菜单、倍速与延迟、媒体信息、置顶及截图入口。 |
| `src/playlist/playlistmodel.{h,cpp}` | 唯一队列、稳定 itemId、状态、去重与行移动。 |
| `src/playlist/playlistcontroller.{h,cpp}` | 选片、播放顺序、随机历史、EOF 与失败上限。 |
| `src/playlist/playlistpanel.{h,cpp}` / `src/playlist/playlisthost.{h,cpp}` | 同一列表的停靠、窄屏覆盖和独立窗口呈现。 |
| `src/playlist/folderscanner.{h,cpp}` | 可取消的后台非递归扫描与自然排序。 |
| `src/playback/player.{h,cpp}` | 持有 `mpv_handle`，异步提交命令，按文件处理事件和属性反馈。 |
| `src/playback/mediastate.h` | mpv 节点转换、当前文件轨道及恢复指纹。 |
| `src/capture/captureservice.{h,cpp}` | 读取源画面/取景副本，后台编码及不覆盖写盘，隔离旧文件的完成提示。 |
| `src/settings/settingsstore.{h,cpp}` | StandardPaths、默认值、INI 校验与原子保存，以及屏幕几何约束。 |
| `src/settings/historystore.{h,cpp}` | 最近 50 项、文件指纹与断点、后台合并写入及失败保留。 |
| `src/settings/sessioncontroller.{h,cpp}` | 连接窗口记忆、最近菜单、断点提示、定时补存及当前文件清除抑制。 |
| `src/settings/settingsdialog.{h,cpp}` | 设置草稿、应用/取消、语言和历史选项、快捷键录制与冲突替换。 |
| `src/video/videowidget.{h,cpp}` | 接收滚轮和拖动输入，在 Qt 的 OpenGL 上下文中保存完整画面，并按当前取景绘制。 |
| `src/video/viewgeometry.h` | 用窗口逻辑像素计算适窗画幅、指针锚点、拖动边界及调整窗口后的构图。 |

源码按 `playback`、`video`、`ui`、`playlist`、`capture`、`settings` 子目录组织。正式程序和实验工具链接共享的 `zopana_core` 静态库，根目录的 CMake 配置负责依赖、应用入口和可选验证工具。libmpv SDK 通过 `MPV_ROOT` 引用。[核心库构建定义](src/CMakeLists.txt)

### 第三方依赖与许可

功能图标使用固定提交的 Lucide SVG。完整 ISC 与 Feather MIT 许可可通过「帮助 → 第三方许可」或「帮助 → 关于 zopana → 第三方许可」查看。[图标清单](resources/icons/lucide/SOURCES.json) · [图标许可证](packaging/notices/lucide-LICENSE.txt)

当前便携包供个人使用。仓库保留第三方依赖的版本、源码和构建来源。对外发布前再选定 zopana 自身的许可证，并整理需要随包提供的第三方许可材料。[第三方依赖来源清单](packaging/notices/SOURCES.txt)
