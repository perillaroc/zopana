# 播放控制阶段 1 验证

`run.ps1` 依次运行偏好重启、配置迁移及定位检查。脚本将日志、校验值、测试配置和媒体副本写入构建目录下每轮单独的证据目录。各探针使用独立配置与历史路径，可在真实用户设置存在时直接运行。

## 构建探针

在 VS 2022 x64 Developer PowerShell 7 中从源码仓库根目录运行。准备 [Windows x64 构建环境](../../../docs/development/build.md)、可打开 Qt/OpenGL 窗口的桌面、`zopana-ui-v1` 素材目录和位于其同级目录的 `zopana-ui-m2-v1/long-grid.mp4`。下面的路径适用于源码位于工作区 `repo/zopana` 时。按实际位置修改 `$qt`、`$mpv` 和 `$fixtures`。

```powershell
$qt = 'C:/lang/qt/6.11.3/msvc2022_64'
$mpv = (Resolve-Path '../../deps/libmpv/0.42.0-windows-x64').Path
$fixtures = '../../test-media/zopana-ui-v1'
$build = '../../build/playback-controls-stage-1-6'
cmake -S . -B $build -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$qt" "-DMPV_ROOT:PATH=$mpv" -DZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE1_PROBE=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build $build --target zopana zopana_playback_preferences zopana_playback_settings zopana_playback_seek
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
```

## 准备测试素材

已有素材时可直接运行检查。需要从源码生成时，在新的目录中运行现有 M0、M2 生成器。需要 Python 3、Pillow 和带 `libx264` 的 FFmpeg。

```powershell
$fixtureRoot = Join-Path $build ('fixtures-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$fixtures = Join-Path $fixtureRoot 'zopana-ui-v1'
python ./experiments/m0/fixtures.py $fixtures
if ($LASTEXITCODE -ne 0) { throw 'Base fixture generation failed' }
python ./experiments/m2/fixtures.py (Join-Path $fixtureRoot 'zopana-ui-m2-v1') --base $fixtures
if ($LASTEXITCODE -ne 0) { throw 'Seek fixture generation failed' }
```

## 运行配置与定位检查

`NativeDpr` 取显示器的原生 Qt DPR，例如 Windows 缩放为 200% 时使用 `2`。偏好检查通过 Qt 进程缩放采样 DPR 1、1.5、2。配置和定位检查使用 DPR 1。运行时保持桌面解锁，让探针窗口获得焦点，并等待脚本结束后再操作窗口。

```powershell
$runner = './experiments/playback-controls/stage-1/run.ps1'
$nativeDpr = 2
& $runner -BuildDir $build -FixtureDir $fixtures -QtRoot $qt -NativeDpr $nativeDpr
```

| `-Checks` 选项 | 验收范围 | 证据目录前缀 |
| --- | --- | --- |
| `Preferences` | P01 中阶段 1 已接入的偏好。每档 DPR 启动写入和读取两个进程，检查关闭历史时的播放偏好和置顶恢复，以及重启后重新适窗。 | `p01-` |
| `Settings` | P02–P03，schema 迁移、未知字段、有效旧按键、按页复位、历史隔离、写保护、已落盘设置在强制终止后恢复。 | `p02-p03-` |
| `Seek` | S01–S04，默认及重启后的步长、菜单与提示、连续定位、首尾限幅和按焦点分配按键。 | `s01-s04-` |

只重跑配置和定位时，在同一命令末尾加 `-Checks Settings,Seek`。每组开头输出证据目录，成功时显示对应进程的 `PASS` 和检查项数。任何一组报错都会让总入口失败，可从最后输出的目录查看日志。`Settings` 只在该轮证据目录设置写保护并恢复其权限，强制终止的是该轮启动的检查进程。

## 章节与撤销定位

用含 `libx264` 的 FFmpeg 生成有章节和无章节的样片，并在两个播放器窗口中检查章首时间、当前章勾选和一次撤销。日志和暂停帧对照图写入 `navigation-` 证据目录。

```powershell
cmake --build $build --target zopana_playback_navigation
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& ./experiments/playback-controls/stage-1/run-navigation.ps1 -BuildDir $build -QtRoot $qt
```

## 逐帧后退

需要 FFprobe、带 `libx264` 的 FFmpeg 和 `zopana-ui-v1` 素材。探针在真实窗口中检查菜单、小窗和自定义快捷键，生成固定帧率、可变帧率和带标记的样片。它用 FFprobe 找到各采样点的上一帧，对照独立定位所得的画面，并将 PNG、日志和 4K 样片的六组用时写入 `frame-step-` 目录。

```powershell
cmake --build $build --target zopana_playback_frames
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& ./experiments/playback-controls/stage-1/run-frame-step.ps1 -BuildDir $build -FixtureDir $fixtures -QtRoot $qt
```

`4k-timings.json` 中的时刻使用同一单调时钟。按键时刻取 Qt 收到应用内按键前，引擎完成取 libmpv 的 `PLAYBACK_RESTART`，显示完成取采样像素首次匹配目标帧时的 `QOpenGLWidget::frameSwapped`。显示耗时截至窗口提交画面。

加 `-Reference` 运行参考帧对照用例。探针使用带标记的样片，验证 90°、8× 下的双向逐帧、显示切换、撤销定位和参考帧清理，并在 `reference-` 目录保留两种显示的剪贴板图像及对应 PNG。此模式会写入系统剪贴板。

加 `-RenderTimeout` 运行 4K 渲染超时用例。探针按下逐帧后退快捷键后暂停处理 GUI 事件 950 毫秒，并验证 libmpv 在此期间报告丢帧和播放重启。恢复处理事件后，用例通过窗口交换时采样的像素确认目标帧已显示，并核对仅发送一次逐帧命令。日志写在 `render-timeout-` 目录。

加 `-SlowSettings` 在测试配置同步到磁盘前人为暂停写入。探针在写入暂停期间用 F7 打开控制窗口，并对照窗口交换时的像素验证 4K 逐帧。随后验证连续修改能自动保存，并在另一次写入暂停时关闭窗口，由新进程读回最后的音量和静音状态。日志写在 `slow-settings-` 目录。

## 旋转、窗口与元数据验证

构建同一目录下的两个额外目标。准备 Python 3、FFprobe 和含 `libx264`、`libvpx-vp9` 的 FFmpeg。

```powershell
cmake --build $build --target zopana_playback_rotation zopana_playback_metadata
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$stage1 = './experiments/playback-controls/stage-1'
& "$stage1/run-rotation.ps1" -BuildDir $build -FixtureDir $fixtures -QtRoot $qt -NativeDpr $nativeDpr
& "$stage1/run-metadata.ps1" -BuildDir $build -FixtureDir $fixtures -QtRoot $qt
```

旋转探针在三档 DPR 打开实际播放器，检查旋转、窗口物理尺寸、字幕、截图和重新启动后的偏好，证据目录以 `rotation-` 开头。元数据小样在前台播放时验证无输出读取和超时回收，证据目录以 `metadata-` 开头。
