# 源码与配置

## 配置与历史文件 {#settings-files}

应用和组织标识固定为 `zopana`。文件位于 Qt 为当前 Windows 用户选择的目录中，便携版也使用这些目录。

| 文件 | 位置 |
| --- | --- |
| 设置 | `QStandardPaths::AppConfigLocation/settings.ini` |
| 播放历史 | `QStandardPaths::AppLocalDataLocation/history.json` |

`settings.ini` 的当前 schema 为 2。旧配置读取时使用已有合法值，缺失或无效字段取默认值。首次成功保存旧配置时写入 `schemaVersion=2`，更高的已有版本号和未知字段均保留。有效自定义键优先于与其冲突的普通默认键，显式清空的绑定也会保留。

设置对话框应用时将已编辑字段与当前运行时偏好合并，再原子替换 INI。菜单和取景产生的偏好变更在约 700 ms 后合并保存，正常关闭时补存。历史 JSON 由工作线程单独写入。保存失败时播放器会提示，当前播放仍可继续。[应用设置和历史操作](../guide/settings.md)

表中字段名使用 QSettings 的 `组/键` 写法。

| 偏好 | 字段 | 默认值 |
| --- | --- | --- |
| 定位步长 | `playback/seekSmallSeconds`、`playback/seekMediumSeconds`、`playback/seekLargeSeconds` | 5、30、60，分别接受 1–3600 的整数秒 |
| 跳略开关 | `skip/enabled`、`skip/introEnabled`、`skip/outroEnabled` | 均为 `false` |
| 跳略秒数 | `skip/introSeconds`、`skip/outroSeconds` | 均为 `0`，接受 0–86400 的整数秒 |
| 倍速与保持音调 | `playback/speed`、`playback/pitchCorrection` | `1`、`true` |
| 循环和随机播放 | `playback/repeatMode`、`playback/shuffleEnabled` | `none`、`false`，循环枚举为 `none`、`one`、`all` |
| 字幕显隐、音频和字幕延迟 | `tracks/subtitleVisible`、`tracks/audioDelay`、`tracks/subtitleDelay` | `true`、`0`、`0`，延迟单位为秒 |
| 置顶 | `window/alwaysOnTop` | `false` |
| 取景 | `video/viewScale`、`video/focusU`、`video/focusV` | `1`、`0.5`、`0.5`，中心按未旋转画面归一化 |

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

阶段 1 的偏好、设置和定位验证入口见仓库内 `experiments/playback-controls/stage-1/README.md`。
