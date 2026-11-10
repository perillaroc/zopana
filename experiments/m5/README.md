# M5 设置、快捷键与播放记忆验证

正式程序通过 SessionController 接入 SettingsStore、HistoryStore 和 SettingsDialog。回归工具给存储模块传入隔离目录，不修改日常用户配置；生产入口使用固定组织/应用标识和 StandardPaths。

从工作区根目录执行：

```powershell
& .\repo\zopana\experiments\m5\build.ps1
& .\repo\zopana\experiments\m5\run.ps1
```

输出在 `build/ui-m5-release-20261110`。`stores` 核对默认值、无效字段、损坏 INI 重写、未知键与未来 schema、50 项上限、元数据变化、清除、后台失败和几何钳制。`m5-100/150/200` 检查设置草稿/应用、录制/冲突替换、原生编辑保护、帮助同步、语言选择、断点三策略与超时、主动播放/定位、停止重播，以及清除后定时器和自动循环抑制。

每档的 `roundtrip-*` 使用两个独立进程保存/读取配置，核对音量/静音、鼠标模式、字体、正常几何/最大化、独立列表形态/显隐、快捷键，以及不恢复全屏、最小化、倍速和裁切。随后在 200% 读取 100% 保存的数据，单独记录 `cross-dpi.json`。

`abrupt-exit.ps1` 启动本工具的隐藏子进程，实际播放长视频并等待每 5 秒的断点落盘；核对其 PID、路径和启动时间后仅终止这个自建进程，再以新进程读取断点。它不会终止用户正在运行的播放器。`termination.json` 与日志明确区分强制终止和正常退出检查。

M0–M4 全部沿用同一正式核心回归；有效 DPR 从原生值校准，不改 Windows 全局缩放。截图来自应用 QWidget/FBO，无桌面捕获。长期媒体继续使用现有四个版本库，测试损坏/元数据变化使用自己的临时文件。

常用单项命令：`zopana_m5 stores|ui|bindings|write|read FIXTURES OUTPUT`。`write/read` 须使用同一个 OUTPUT；每个运行进程只保留一个播放核心。保存与读取结果分别保存在 `write-results.json/read-results.json`。

几何测试可模拟屏幕移除、负坐标和超大/无效矩形；本机只有一块物理屏幕，实际拔屏和跨屏输入仍按 M6 实测。真实 IME、物理键盘布局、独立无 SDK 系统与长期资源/音频听感也保留在 M6。`ZOPANA_BUILD_M5_PROBE` 默认关闭。

验证脚本使用本地命名互斥锁，窗口测试必须串行运行。打包为新的 `dist-final` 后，沿用 `experiments/m2/verify-package.py BUILD MEDIA_FILE` 核对部署；运行 `python experiments/m5/evidence.py BUILD TEST_MEDIA_ROOT` 汇总所有阶段结果、校验长期素材并记录实际工作树和二进制 SHA-256。
