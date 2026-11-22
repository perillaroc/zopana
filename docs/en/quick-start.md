# Quick start

The zopana portable build runs on Windows 11 x64. Get `zopana-windows-x64.zip` and its matching `zopana-windows-x64.zip.sha256` file. You can also build the player or create the portable package from source. [Build from source](./development/build.md) · [Create a portable package](./development/packaging.md)

## Verify and extract the ZIP

Open PowerShell in the folder containing both files. Compare the value in the `Hash` column with the checksum in the `.sha256` file.

```powershell
Get-FileHash -Algorithm SHA256 .\zopana-windows-x64.zip
```

Extract the entire ZIP and double-click `zopana-windows-x64\zopana.exe`. The package includes Qt plugins, libmpv, and the VC++ x64 runtime. Keep the extracted folder together when moving the player.

## Play your first video

1. Click **Open File** (打开文件) or press **Ctrl+O** and choose a local video. Playback starts automatically.
2. Click **Pause** (暂停) and check that the top bar shows **Zoom and Pan** (缩放拖动). Place the pointer over a detail, scroll up to zoom in, and hold the left mouse button to drag the picture.
3. Click **Fit to Window** (恢复适窗) or press **R** over the video to see the full picture. Switch to **Normal Playback** (常规播放) to use the mouse wheel for volume.

Press **F1** for current shortcuts and **F5** for Settings (设置). Playback history starts off. Open Playback and History (播放与记录) in Settings to remember recent files and resume positions. [Playback history](./guide/settings.md#resume-playback) · [Mouse modes](./guide/zoom-and-pan.md) · [Playback and full screen](./guide/playback.md)
