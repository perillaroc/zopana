# zopana M0 验证小样

本目录单独构建，直接编译现有 `MainWindow`、`Player` 和 `VideoWidget`，不修改正式构建入口或生产源码。用于验证 Qt 覆盖层、全屏上下文、两种截图、字幕缩放、libmpv 常用命令及重建恢复。

## 运行

在工作区根目录的 PowerShell 执行：

```powershell
# 已有素材库时跳过生成。首次生成使用一个新目录。
python .\repo\zopana\experiments\m0\fixtures.py .\test-media\zopana-ui-v1
& .\repo\zopana\experiments\m0\build.ps1
& .\repo\zopana\experiments\m0\run.ps1
```

默认使用 Qt 6.11.3 的 MSVC 2022 x64 kit、Visual Studio 2022，以及工作区 `deps/libmpv/0.42.0-windows-x64`。构建脚本可通过 `-BuildDir`、`-QtRoot` 和 `-VsRoot` 指定其他路径；运行脚本接受 `-BuildDir`、`-QtRoot`、`-FixtureDir`。Python 需要 Pillow 和 NumPy，FFmpeg/FFprobe 需要在 PATH 中。

默认输出到工作区 `build/ui-m0-20261109`：

| 文件 | 内容 |
| --- | --- |
| `configure.log` / `build.log` / `compiler.log` | 工具链与构建结果 |
| `build-id.txt` | 两个 EXE 和 libmpv DLL 的 SHA-256 |
| `environment/results.json` | 原生有效 DPR 校准 |
| `baseline-100/150/200` | 原始 MainWindow 的空态、窗口/全屏和两模式截图 |
| `baseline-100/results.json` | 1080p/4K 约 6 秒采样的性能基线 |
| `probe-100/150/200` | 实际运行断言、请求回复、文件事件和截图 |
| `pixel-analysis.json` | 三档 DPI 的方向、色彩、黑边、原始截图不变与字幕缩放/裁切检查 |
| `probe-source-hashes.json` | 小样源码与脚本身份 |

输出目录中的历史失败日志可在下一次复跑时被覆盖；需要保留历史运行时，指定新的构建/输出目录。长期素材固定保存在工作区 `test-media/zopana-ui-v1`，不依赖 build 目录。

## 验证范围

`baseline` 模式直接构造生产 MainWindow；`probe` 模式为相同 VideoWidget 增加兄弟控件，使用 Qt 命中测试与 QTest 事件验证输入归属。Qt 的 widgetAt 受桌面遮挡影响，因此不作为判定依据；这些检查不替代后续真实鼠标、输入法和完整焦点回归。

附加的 `m0-observer` 是 `mpv_create_client` 创建的同一播放核心客户端，不是第二个播放器。实验命令与回复在该客户端处理，避免未知 requestId 进入现有 Player 的固定枚举计数逻辑。

三档 DPI 通过进程级 `QT_SCALE_FACTOR` 相对校准值实现，报告同时记录实际 widget DPR。脚本不更改 Windows 全局显示缩放；物理多屏混合 DPI 和实时系统 DPI 切换留给 A03/M6 验收。

性能数字包含小样观察客户端及采样开销。首帧指标定义为发出打开请求至 Loaded 后首个有效 frameSwapped，属于可呈现首帧近似；不是独立解码器时间。后续比较必须使用同一仪器、素材、窗口、DPI 和硬件环境。

正式实现仍需把状态恢复和请求隔离接入 Player/队列层。小样中的恢复步骤用于证明路线可行，不能当作生产代码已具备该能力。
