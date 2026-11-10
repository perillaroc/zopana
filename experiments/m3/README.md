# M3 播放列表验证

验证正式 Qt 队列、稳定 itemId、播放顺序、失败上限、后台扫描、列表输入、停靠/覆盖/独立形态和主窗口生命周期；与 M0–M2 共用正式 zopana_core。

从工作区根目录执行：

```powershell
# 已保留素材时跳过生成。
python .\repo\zopana\experiments\m3\fixtures.py .\test-media\zopana-ui-m3-v1 --base .\test-media\zopana-ui-v1
& .\repo\zopana\experiments\m3\build.ps1
& .\repo\zopana\experiments\m3\run.ps1
```

固定素材包含自然排序的 E1/E2/E10、三个坏文件、同名字幕、应忽略的子目录以及 1000 个独立路径。素材可能使用 NTFS 硬链接共享原库数据，保持不可变；修改内容应建立新版本副本。

输出在 `build/ui-m3-release-20261110`：`m3-100/150/200` 保存列表检查及截图，`m2-*`、`qa-*` 和 `m0-regression` 是既有功能回归。`ZOPANA_BUILD_M3_PROBE` 默认关闭。

验证使用 QAbstractItemModelTester、QTest、实际 libmpv、Qt 窗口属性与应用自身截图。它不启动桌面捕获。物理鼠标、真实输入法和多屏 DPI 继续在 M6 实测；若环境只有一块屏幕，报告会明确记录 screens=1，不能记为物理第二屏通过。

`zopana_m3 --lifecycle FIXTURES OUTPUT` 单独启动 Qt 事件循环，验证主窗口关闭后独立列表不会阻止应用退出。
