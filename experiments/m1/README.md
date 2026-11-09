# M1 界面验证工具

此目录通过可选构建开关 `ZOPANA_BUILD_M1_PROBE` 接入，默认关闭。正式程序和验证工具链接相同的 `zopana_core`，从而直接验证实际 MainWindow、共享 QAction、控制栏、状态页和主题。

## 构建与验证

从工作区根目录执行：

```powershell
& .\repo\zopana\experiments\m1\build.ps1
& .\repo\zopana\experiments\m1\run.ps1
```

默认使用工作区 `deps/libmpv/0.42.0-windows-x64`、Qt 6.11.3 MSVC x64 kit 和 VS 2022；Qt 需要 Widgets、OpenGL、OpenGLWidgets、Svg，验证工具另需 Test。构建脚本接受 `-BuildDir`、`-QtRoot`、`-VsRoot`，运行脚本接受 `-BuildDir`、`-QtRoot`、`-FixtureDir`。Python 分析需要 Pillow 和 NumPy。

素材直接复用工作区 `test-media/zopana-ui-v1`，不覆盖 M0 原始数据。默认构建与证据目录为 `build/ui-m1-release-20261109`，产物包括正式 `zopana.exe`、`zopana_m1.exe` 和 `zopana_m0_regression.exe`。普通发行构建不需要 Qt Test，不会生成这两个验证工具。

## 检查内容

- 空态、加载、播放、暂停、音频、错误、停止与自然结束的真实状态；未接入的停止按钮不作为正式功能暴露。
- 1120 × 720、800 × 500、640 × 400 窗口的实际尺寸、按钮边界、时间文字宽度、折叠菜单和无障碍名称。
- Ctrl+O 打开文件、Space 播放、滑块方向键、按钮 Space、Tab 导航、R 复位、F11/Enter/Esc 全屏，以及文本/IME/模态保护。
- 高对比度分支去除 QSS、使用系统颜色与文字图标；回到深色后通过渲染像素检查菜单和错误页，防止遗留调色板。
- 按照实际 Qt 调色板计算对比度，按 SHA-256 和 Git blob 同时核对 Lucide 资源。
- 用当前生产渲染代码复跑 M0 的全屏、FBO、截图、字幕与恢复验证，并运行其独立像素分析。

## 输出

| 路径 | 内容 |
| --- | --- |
| `qa-100/150/200` | 每档有效 DPR 的结果、布局数据及截图 |
| `m0-regression/probe-100/150/200` | M0 运行回归，结果不写入旧阶段目录 |
| `m0-regression/pixel-analysis.json` | M0 图像回归结果 |
| `asset-contrast-analysis.json` | UI 通过状态、DPR、对比度与图标来源校验 |
| `configure.log` / `build.log` / `build-id.txt` | 配置、编译与程序/SDK 身份 |

这里的 DPI 是通过 `QT_SCALE_FACTOR` 校准的进程内有效 DPR，脚本不会更改 Windows 的全局显示设置。高对比度分支由验证工具显式调用，未切换 Windows 全局辅助功能。QTest 的文件选择使用 Qt 对话框以便自动输入路径；原生 Windows 文件对话框、实际输入法与物理多屏还需对应人工/系统验收。

后续复跑会覆盖该构建目录内的日志和截图；需要保留另一轮证据时指定新的 `-BuildDir`。正式窗口没有验证开关或额外测试入口。
