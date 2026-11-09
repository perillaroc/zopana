zopana · Windows x64 自用便携版

启动
1. 在 Windows 11 x64 上解压整个 ZIP，保留 zopana-windows-x64 中的所有文件和子目录。
2. 双击 zopana.exe。“打开文件”或 Ctrl+O 选择一段本地视频。播放器使用 Windows 的当前音频输出设备。
3. 点击“暂停”。默认的“缩放拖动”模式下，在视频区把指针指向细节，上滚放大，按住左键拖动画面。
4. 切到控制行的“常规播放”，在视频区上滚一格将音量增加 5 个百分点。单击松开切换播放或暂停。
5. 点击恢复适窗图标（视频区聚焦时也可按 R）查看完整画面。F11 切换全屏，Esc 退出全屏，关闭窗口即可退出。

便携包随附 Qt 运行库、Windows 平台插件、OpenGL 软件后备、libmpv 和程序本地的 Microsoft Visual C++ x64 运行库。上述文件可与程序一起移动。若运行库文件被移走，重新完整解压。使用系统级运行库的管理员可安装微软的 Visual C++ v14 x64 Redistributable：
https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist

文件校验
在打包输出目录的 PowerShell 中运行 Get-FileHash -Algorithm SHA256 .\zopana-windows-x64.zip，核对同目录中的 zopana-windows-x64.zip.sha256。

图标许可
Lucide 图标的完整 ISC 与 Feather MIT 许可保存在 notices/lucide-LICENSE.txt，应用内可通过“帮助 → 第三方许可”查看。
