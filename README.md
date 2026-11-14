# zopana

zopana 是一个 Windows 桌面视频播放器，使用 Qt Widgets 和 libmpv。播放本地视频时，可跟随鼠标指针缩放并拖动画面，也可以切换到常规播放模式，用滚轮调音量、单击播放或暂停。

## 项目文档

- [快速开始](docs/quick-start.md) · 校验和解压 Windows x64 便携包，打开第一段视频。
- [使用手册](docs/guide/playback.md) · 播放、全屏和快捷键，以及[缩放拖动](docs/guide/zoom-and-pan.md)、[播放列表](docs/guide/playlist.md)、[音轨和字幕](docs/guide/media.md)、[截图和设置](docs/guide/settings.md)。
- [开发指南](docs/development/build.md) · 准备 Qt 和 libmpv、构建播放器、[制作和验收便携包](docs/development/packaging.md)。
- [播放控制集成验收](experiments/playback-controls/stage-6/README.md) · 复现组合播放、全量偏好重启、窗口压力和性能对照。
- [文档维护与部署](docs/development/docs.md) · 本地预览、构建和发布到 Netlify。

## 本地预览文档站

在安装了 Node.js 22 的 Windows PowerShell 中，从仓库根目录执行下列命令，再打开终端给出的本地地址。

```powershell
cd docs
npm.cmd ci
npm.cmd run docs:dev
```
