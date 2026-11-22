<img align="right" src="resources/brand/zopana.svg" alt="zopana 图标" width="80" height="80">

# zopana

简体中文 · [English](README.en.md)

[![Windows CI 状态](https://github.com/perillaroc/zopana/actions/workflows/ci.yml/badge.svg?branch=main&event=push)](https://github.com/perillaroc/zopana/actions/workflows/ci.yml)
[![Windows 11 x64](https://img.shields.io/badge/Windows-11%20x64-0078D4)](#安装)
[![Apache 2.0 许可证](https://img.shields.io/github/license/perillaroc/zopana)](LICENSE)

zopana 是一款 Windows 本地视频播放器，可以放大鼠标指向的区域，拖动画面查看细节。

[发布与下载](https://github.com/perillaroc/zopana/releases) · [安装](#安装) · [使用手册](docs/guide/playback.md)

## 功能简介

- **画面取景与对照** 支持 1×–8× 缩放、旋转、临时查看全图和固定参考帧，可在参考帧与当前画面之间切换。[缩放与取景](docs/guide/zoom-and-pan.md)
- **播放控制** 支持三档步长定位、前后逐帧、倍速、A–B 循环和按设定秒数跳过片头片尾。[播放与窗口](docs/guide/playback.md) · [进阶播放](docs/guide/media.md)
- **本地播放列表** 支持批量添加文件和文件夹、搜索与排序、循环与随机播放，以及导入导出本地 `.m3u8` 片单。[文件与播放列表](docs/guide/playlist.md)
- **音轨与字幕** 可切换音轨和字幕，加载或自动匹配外挂字幕，并调整字幕大小及音频、字幕延迟。[音轨与字幕](docs/guide/media.md)
- **截图与复制** 可保存完整视频画面或带可见字幕的当前取景，也可将取景复制到剪贴板。[截图与设置](docs/guide/settings.md)
- **播放历史** 可在设置中开启播放历史，记住最近文件并从保存的断点继续播放。[播放历史](docs/guide/settings.md)

## 安装

便携版适用于 **Windows 11 x64**，运行库随包提供。

1. 便携包发布后，在 GitHub Releases 的 **Assets** 中下载 `zopana-windows-x64.zip`。
2. 完整解压 ZIP，再双击 `zopana-windows-x64\zopana.exe`。移动播放器时请保留整个解压目录。

可以一并下载 `zopana-windows-x64.zip.sha256`，核对压缩包的完整性。[校验与解压步骤](docs/quick-start.md)

## 使用

点击底栏的 **打开文件** 选择本地视频，或把视频拖入窗口。

首次启动使用 **缩放拖动** 模式。把指针移到想看的细节上，滚轮放大，按住左键移动画面。点击 **恢复适窗** 可以回到完整画面。

点击顶栏的模式按钮切到 **常规播放**。在这个模式下，视频区滚轮用于调节音量，左键单击可暂停或继续。[两种鼠标模式](docs/guide/zoom-and-pan.md)

## 开发

zopana 使用 C++20、Qt Widgets 和 libmpv，在 Windows x64 上用 Visual Studio 2022、CMake 和 Ninja 构建。[构建与启动](docs/development/build.md) · [打包与发布](docs/development/packaging.md) · [播放控制验收](experiments/playback-controls/stage-6/README.md) · [文档维护](docs/development/docs.md)

## 协议

zopana 自有源码采用 Apache License 2.0（`Apache-2.0`）。第三方组件适用各自的许可。[许可证全文](LICENSE) · [第三方许可与来源](packaging/notices/SOURCES.txt)
