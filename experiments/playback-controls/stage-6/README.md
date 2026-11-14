# 播放控制集成验收

用与应用相同的 MSVC x64 Release、Qt 6.11.3 和 libmpv 0.42.0 构建。验证目录须新建；探针将设置、历史、测试素材和日志留在验收目录。界面操作使用 Qt 自动注入，执行期间保持桌面可以显示测试窗口。

```powershell
cmake -S . -B $build -DZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE6_PROBE=ON
cmake --build $build --target zopana zopana_playback_release --parallel
& .\experiments\playback-controls\stage-6\run.ps1 -BuildDir $build -FixtureDir $fixtures -ReportDir $reports -NativeDpr 2
```

`$fixtures` 指向共享的 `zopana-ui-v1`，旁边应有 `zopana-ui-m2-v1/long-grid.mp4`。脚本需要 PowerShell 7 和 FFmpeg。素材重新封装时保留音视频流，并重排一份双音轨、双字幕素材的流序。写入／读取使用独立进程；窗口压力包括 32 轮和两次真实 GL 上下文销毁及重建。报告记录每项检查、资源和文件 SHA-256。

- C02：启用 `ZOPANA_BUILD_M2_PROBE`、`ZOPANA_BUILD_M3_PROBE`、`ZOPANA_BUILD_M4_PROBE`、`ZOPANA_BUILD_M6_PROBE`，复用相应探针。原生 DPR 为 2 时取 `QT_SCALE_FACTOR=0.5/0.75/1`；设置 `QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough`。这些样本不替代物理跨屏。
- C03：同机用 `ZOPANA_BUILD_M1_PROBE` 构建当前版本与阶段 1 前的 `aaf036aebabb5ef04efa74ca8790267c53ae3432`。将当前的 `experiments/m0/probe.cpp` 复制到基线源码副本，保持产品源码原样。两边使用相同编译器、SDK、DPR 1、窗口大小、素材、`production-perf <zopana-ui-v1> <新目录>` 参数。轮换两版本至少三对，记录启动到探针准备、提交打开到首帧、约六秒的进程 CPU、工作集、专用内存及丢帧增量。千项开关对照复用[阶段 5 的 throughput 入口](../stage-5/README.md)，记录同一前台播放下扫描前和扫描头六秒的资源及进度。
- C04、P02–P03：额外运行 `zopana_m6 stress`（WASAPI 电平）、`zopana_m4 --engine-recovery`（实际销毁渲染视图），以及[配置读写、写保护与强制退出测试](../stage-1/README.md)。
- C05–C06：见[物理输入与独立 Windows 验收](../../m6/README.md)。`check-clean-windows.ps1` 的 `-StartupOnly` 结果仅代表当前宿主机。在独立环境运行完整模式并逐项确认，才形成独立 Windows 记录。
