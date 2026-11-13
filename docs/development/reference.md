# 源码与配置

## 配置与历史文件 {#settings-files}

应用和组织标识固定为 `zopana`。文件位于 Qt 为当前 Windows 用户选择的目录中，便携版也使用这些目录。

| 文件 | 位置 |
| --- | --- |
| 设置 | `QStandardPaths::AppConfigLocation/settings.ini` |
| 播放历史 | `QStandardPaths::AppLocalDataLocation/history.json` |

设置保留有效字段和未知字段，损坏值回退到默认值。历史 JSON 在工作线程原子替换。设置或历史保存失败时，播放器会提示，当前播放仍可继续。历史开关、清除记录和断点策略见[截图与设置](../guide/settings.md)。

## 源码布局

正式程序和实验工具链接共享的 `zopana_core` 静态库。根目录的 `CMakeLists.txt` 管理应用入口、依赖和可选验证工具。`src/CMakeLists.txt` 定义核心库，libmpv SDK 通过 `MPV_ROOT` 引用。

| 路径 | 职责 |
| --- | --- |
| `src/main.cpp` | 创建 Qt 应用和播放器，启动主窗口并管理退出顺序。 |
| `src/ui/` | 主窗口、共享 QAction、控制栏、视频状态页、主题、快捷键帮助及媒体菜单。 |
| `src/playback/` | libmpv 生命周期、异步命令、按文件处理的事件和轨道状态。 |
| `src/video/` | 视频区输入和 OpenGL 渲染，计算适窗、指针锚点及拖动边界。 |
| `src/playlist/` | 播放队列、播放顺序、停靠与独立列表，以及可取消的文件夹扫描。 |
| `src/capture/` | 原始画面和当前取景截图、后台编码及避免覆盖已有文件。 |
| `src/settings/` | INI 配置、快捷键编辑、窗口记忆、最近文件和断点续播。 |
| `experiments/` | 可选的阶段验证工具和复现说明。 |
| `packaging/` | Windows 便携包脚本、随包说明和第三方依赖来源。 |
