# M2 常用播放与全屏验证

直接使用正式 `zopana_core`、MainWindow、VideoWidget 和 libmpv，覆盖停止/EOF/重播、合并定位、音量静音、键盘别名、双击与取消手势、进度预览、时间输入、帮助、OSD、拖放及全屏隐藏。测试命令由同一核心的附加客户端执行，不建立第二个播放器。

从工作区根目录运行：

```powershell
# 仅首次生成，已保留素材时跳过。
python .\repo\zopana\experiments\m2\fixtures.py .\test-media\zopana-ui-m2-v1 --base .\test-media\zopana-ui-v1
& .\repo\zopana\experiments\m2\build.ps1
& .\repo\zopana\experiments\m2\run.ps1
```

默认产物在 `build/ui-m2-release-20261109`。`m2-100/150/200/results.json` 保存 M2 检查，`qa-*` 是 M1 回归，`m0-regression` 保存 M0 运行及像素回归。三档 DPR 按环境实测校准，不改 Windows 全局设置；截图通过本应用 QWidget/FBO 获取，不启动桌面捕获会话。

保留两个素材库：原 `test-media/zopana-ui-v1` 不变；旁边 `zopana-ui-m2-v1` 增加 137 秒网格视频，验证真正的 ±30/60 秒位移，不能仅用短片的边界限位代替。

QTest 注入覆盖 Qt 事件和焦点规则；不能代替物理鼠标、真实 IME、不同键盘布局、多屏 DPI 和独立干净系统的最终验收。`ZOPANA_BUILD_M2_PROBE` 默认关闭，发行构建不依赖 Qt Test。

打包到该构建目录的 `dist-final` 后，可运行以下部署验证。它校验 ZIP 与 Release，再临时把同 core 的 `zopana_m2_smoke` 放入解压目录；移除 Qt 环境变量并仅保留 Windows PATH，验证加载、暂停、全屏及正常退出，记录实际 DLL 路径，最后移除临时 EXE。

```powershell
python .\repo\zopana\experiments\m2\verify-package.py .\build\ui-m2-release-20261109 .\test-media\zopana-ui-v1\grid.mp4
```
