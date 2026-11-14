# 便携包与发布验收

## 制作 Windows x64 便携包

在 VS x64 开发终端完成 [Release 构建](./build.md#build-player)后，使用相同的 Qt kit 和 libmpv SDK 运行打包脚本。下面的命令沿用构建时设置的 `$build`、`$qt` 和 `$mpv`，每次使用新的输出目录。

```powershell
$dist = Join-Path $PWD ('build\package-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
& .\packaging\package-windows.ps1 -BuildDir $build -QtRoot $qt -MpvRoot $mpv -OutputDir $dist
```

脚本用 `windeployqt` 部署 Qt 和平台插件，从指定 SDK 复制 libmpv，并从 VS 的 x64 可再发行目录复制 VC++ 运行库。输出 `zopana-windows-x64.zip` 和 `zopana-windows-x64.zip.sha256`。随包 `README.txt` 提供启动和控制说明；`notices/manifest.json` 记录除清单自身外每份文件的字节数与 SHA-256。校验、解压和启动步骤见[快速开始](../quick-start.md)。

## 在独立 Windows 上验收

在干净的 Windows 11 x64 安装或虚拟机中保留系统原生 PATH。准备 ZIP、校验文件和 `experiments/m6/README.md` 所列的验收工具。脚本会核对 ZIP 和包内清单，将包解压到含中文和空格的目录，检查 Qt 和 libmpv 的实际加载路径，再以同一 EXE 的独立工作入口读取测试片的时长和分辨率。图形界面使用单独的验收配置目录，并由操作人验证播放、输入、独立列表、截图和设置。

在普通 64 位 PowerShell 中运行下面的命令，逐项操作播放器并在终端确认。`result.json` 记录系统、文件哈希和结果，`modules.csv` 记录加载的 DLL，`metadata-worker.jsonl` 保留探测回执。开发机上的 `-StartupOnly -Fixture <有声视频>` 可以复核这些自动项目，结果会标明仅在宿主机启动。

```powershell
$expected = ((Get-Content .\zopana-windows-x64.zip.sha256 -Raw).Trim() -split '\s+')[0]
& .\check-clean-windows.ps1 -Archive .\zopana-windows-x64.zip -ExpectedSha256 $expected
```

## 第三方依赖与许可

功能图标来自 Lucide。完整的 ISC 与 Feather MIT 许可可在应用内通过「帮助 → 第三方许可」查看，随包副本位于 `notices/lucide-LICENSE.txt`。

当前便携包供个人使用。包内 `notices/SOURCES.txt` 列出依赖版本、源码和构建来源，同目录收录 libmpv SDK 的许可、Qt 的 LGPLv3/GPLv3 文本和两份 Qt 组件 SPDX 清单。对外发布前需选定 zopana 自身的许可证，并核对分发所需的材料。
