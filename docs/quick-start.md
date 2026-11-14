# 快速开始

在 Windows 11 x64 上运行 zopana 便携版。准备 `zopana-windows-x64.zip` 和同批交付的 `zopana-windows-x64.zip.sha256`。有项目源码时，也可以[构建播放器](./development/build.md)或[制作便携包](./development/packaging.md)。

## 校验并解压

在两个文件所在的目录打开 PowerShell，运行下面的命令。核对 `Hash` 一列与 `.sha256` 文件中的校验值。

```powershell
Get-FileHash -Algorithm SHA256 .\zopana-windows-x64.zip
```

完整解压 ZIP，双击 `zopana-windows-x64\zopana.exe`。Qt 插件、libmpv 和 VC++ x64 运行库随包提供，移动播放器时请保留整个解压目录。

## 播放第一段视频

1. 点击 **打开文件** 或按 **Ctrl+O**，选择一个本地视频。文件打开后会自动播放。
2. 点击 **暂停**，确认顶栏显示 **缩放拖动**。把指针移到想看的细节上，向上滚动放大，按住左键拖动画面。
3. 点击 **恢复适窗** 或在视频区按 **R**，回到完整画面。切换到 **常规播放** 后，视频区滚轮用于调节音量。

按 **F1** 可以查看当前快捷键，按 **F5** 打开设置。播放历史默认关闭；如需记住最近文件和断点，可在[播放与记录](./guide/settings.md#resume-playback)中开启。继续了解[两种鼠标模式](./guide/zoom-and-pan.md)或[播放与全屏](./guide/playback.md)。
