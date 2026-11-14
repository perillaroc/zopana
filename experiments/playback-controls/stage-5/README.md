# 播放控制阶段 5 验证

探针验证 11 个排序字段、稳定条目 ID、列表入口、播放连续性、独立工作进程和排序偏好。每轮将媒体副本、设置、报告和 SHA-256 写入构建目录下的 `sort-*`。不会改动共享素材或用户设置。

从工作区根目录在 VS 2022 x64 Developer PowerShell 7 下运行。准备 Qt MSVC x64、Ninja、Python 3、FFprobe 和带 `libx264`、`libvpx-vp9` 的 FFmpeg。素材目录与 [阶段 1](../stage-1/README.md) 及 [M3](../../m3/README.md) 相同，完整检查需要同级的 `zopana-ui-m2-v1/long-grid.mp4` 和 `zopana-ui-m3-v1/large`。

```powershell
$build = 'build/playback-controls-stage-5'
$qt = 'C:/lang/qt/6.11.3/msvc2022_64'
$mpv = (Resolve-Path 'deps/libmpv/0.42.0-windows-x64').Path
cmake -S repo/zopana -B $build -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$qt" "-DMPV_ROOT:PATH=$mpv" -DZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE5_PROBE=ON
cmake --build $build --target zopana_playlist_sort
& ./repo/zopana/experiments/playback-controls/stage-5/run.ps1 `
    -BuildDir $build -FixtureDir 'test-media/zopana-ui-v1' -QtRoot $qt -NativeDpr 2
```

`NativeDpr` 使用当前显示器的原生 DPR；`-Dpr` 可选择 1、1.5、2。界面检查在每档运行，其余检查在首档运行。保持桌面解锁，避免在界面检查中抢占焦点。`-Checks comparators,ui` 可只复跑比较器与界面；重启检查使用 `-Checks write,read`。

`throughput` 在前台播放 137 秒的带音轨视频，同时扫描和重排 1,000 项。它记录任务耗时、布局提交、Qt 定时器最大间隔、播放推进、丢帧计数以及子进程 CPU 和峰值工作集。`recovery` 分别模拟子进程读取无响应时的取消、超时和关窗。运行器另用管道启动正式 `zopana.exe` 的工作入口，验证 Unicode 文件路径、标题和编码尺寸。
