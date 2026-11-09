zopana · Windows x64 自用便携版

启动
1. 在 Windows 11 x64 上解压整个 ZIP，保留 zopana-windows-x64 中的所有文件和子目录。
2. 双击 zopana.exe。“打开文件”或 Ctrl+O 选择一段本地视频。播放器使用 Windows 的当前音频输出设备。
3. 点击“暂停”。默认的“缩放拖动”模式下，在视频区把指针指向细节，上滚放大，按住左键拖动画面。
4. 切到控制行的“常规播放”，在视频区上滚一格将音量增加 5 个百分点。单击等待系统双击间隔后切换播放；双击只切换全屏。Ctrl+滚轮临时缩放。
5. 视频区 S 停止、Space 重播，M 静音，方向键定位或调音量，V 切换模式，+/− 缩放，R 恢复适窗。Ctrl+G 输入跳转时间，F1 查看完整快捷键帮助。
6. F11 或双击切换全屏。全屏底栏在无输入 2.5 秒后隐藏，鼠标移动或播放按键唤出；菜单、拖动或控件焦点会阻止隐藏。Esc 一次处理一层，关闭窗口退出。
7. 支持单个本地媒体拖放，以及向已加载媒体拖入 SRT/ASS/SSA/VTT 字幕。多文件和文件夹会提示，不会只播放其中一个并丢弃其余文件。

便携包随附 Qt 运行库、Windows 平台插件、OpenGL 软件后备、libmpv 和程序本地的 Microsoft Visual C++ x64 运行库。上述文件可与程序一起移动。若运行库文件被移走，重新完整解压。使用系统级运行库的管理员可安装微软的 Visual C++ v14 x64 Redistributable：
https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist

文件校验
在打包输出目录的 PowerShell 中运行 Get-FileHash -Algorithm SHA256 .\zopana-windows-x64.zip，核对同目录中的 zopana-windows-x64.zip.sha256。

图标许可
Lucide 图标的完整 ISC 与 Feather MIT 许可保存在 notices/lucide-LICENSE.txt，应用内可通过“帮助 → 第三方许可”查看。
