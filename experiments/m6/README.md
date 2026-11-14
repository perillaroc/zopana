# M6 发布验收复现

## 独立 Windows x64

准备新的 Windows 11 x64 安装或干净虚拟机，保持系统原生 PATH。把便携 ZIP、随包 SHA-256 文件和验收工具复制到同一目录。验收工具包含此目录下的脚本及来自 `zopana-ui-v1` 的合成有声素材 `sample-motion.mp4`。

在普通 64 位 PowerShell 中运行以下命令。可用记事本打开 SHA-256 文件，先与交付记录核对，再运行脚本。

```powershell
$expected = ((Get-Content .\zopana-windows-x64.zip.sha256 -Raw).Trim() -split '\s+')[0]
& .\check-clean-windows.ps1 -Archive .\zopana-windows-x64.zip -ExpectedSha256 $expected
```

逐项操作播放器并在终端确认结果。脚本输出目录中的 `result.json` 记录 ZIP 和各项结果，`modules.csv` 记录实际加载的 DLL，`metadata-worker.jsonl` 记录同一 EXE 从中文路径读取的元数据。交回这些文件及失败时的 `player.log`。`-InspectOnly` 校验归档和逐文件清单；`-StartupOnly -Fixture <有声视频>` 还会验证启动、DLL、退出和元数据入口。宿主机启动结果标记为 `HOST_STARTUP_ONLY`；完整独立环境验收标记为 `INDEPENDENT_PASS`。启动使用验收目录内单独的配置与历史位置。

如环境具备第二块物理屏幕和中文输入法，再补做以下检查并记录屏幕分辨率、各自的 Windows 缩放和设备类型。

- 用真实鼠标在常规模式单击、双击、Ctrl+滚轮和拖动后失焦。核对单击仅切换一次播放，双击只切换全屏，取消后的旧拖动不会触发播放或定位。
- 在打开文件和设置文本框中完成中文预编辑、候选选择和上屏。核对视频区快捷键未触发。关闭输入框后，用数字小键盘 +、−、Enter 验证缩放及全屏。
- 把独立列表和主窗口分别移到两块不同 DPI 的屏幕。切换全屏、停靠/独立，关闭并重启，然后断开列表所在屏幕。核对选中项和可见窗口位置，确认始终只有一个播放器。
- 在听得到音调的音量下播放合成素材，持续至少 10 秒做指针缩放和拖动。确认听感连续，记录所用输出设备。

## 开发机

1. 在已有 VS x64、Qt 6.11.3 和 libmpv 0.42.0 的环境中，运行 `experiments/m5/build.ps1` 建立新的 Release 目录。用相同 CMake 缓存打开 `ZOPANA_BUILD_M6_PROBE`，再构建所有目标。
2. 运行 `experiments/m5/run.ps1 -BuildDir <Release目录>`，获取 M0–M5 三档 DPI 与设置重启回归。
3. 运行 `experiments/m6/run.ps1 -BuildDir <Release目录> -FixtureDir <zopana-ui-v1目录> -BaselineExe <B04原始zopana_m0.exe> -ReportDir <新报告目录>`。`-ScaleFactor` 取屏幕原生 DPR 的倒数，使 100% 样本保持有效 DPR 1。此轮包含默认键路由、数字小键盘扫描码、取景几何、20 次混合窗口压力、WASAPI 电平采样与三对 B04 性能样本。运行时保持测试窗口可获取焦点。
4. 用 `packaging/package-windows.ps1` 打包到 Release 目录下的新 `dist-final`。运行 `python experiments/m2/verify-package.py <Release目录> <有声测试文件> --extract-to <新的中文目录>`，核对 ZIP、SHA-256、嵌入许可证随包副本和移除开发 PATH 后的同核播放。可在新测试用户下运行 `check-clean-windows.ps1 -StartupOnly`，验证原始便携 EXE 本身。
5. 运行 `python experiments/m5/evidence.py <Release目录> <素材库根目录>` 核对各档结果、素材清单和源码身份。用 `python experiments/m6/evidence.py <报告目录>` 汇总 M6 附加结果。
