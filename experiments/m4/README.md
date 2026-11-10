# M4 多轨、截图与状态恢复验证

验证正式 `zopana_core` 的轨道与失败回退、同名匹配、倍速/延迟/字体、真实帧步进、A–B、源画面/取景截图、信息与置顶，以及销毁重建真实渲染器后的完整会话恢复。

从工作区根目录执行：

```powershell
# 现有素材保留即可；生成器拒绝覆盖已有版本。
python .\repo\zopana\experiments\m4\fixtures.py .\test-media\zopana-ui-m4-v1 --base .\test-media\zopana-ui-v1
& .\repo\zopana\experiments\m4\build.ps1
& .\repo\zopana\experiments\m4\run.ps1
```

输出在 `build/ui-m4-release-20261110`。脚本校准原生 DPR，再按 1/1.5/2 运行 M4 与 M0–M3 回归，执行像素、图标原字节和对比度分析。所有图片由应用自身 QWidget/FBO 生成，不启动桌面捕获，不修改 Windows 全局缩放。

M4 素材库提供同基名、en/zh-Hans 后缀、应排除的非语言后缀和 SSA 扩展；可能与基础库共享硬链接，保持不可变。缺失字幕恢复用输出目录的独立临时副本验证，删除不会触及共享素材。

截图在测试输出目录保存；验证源画面缩放不变、取景等于 FBO、物理像素尺寸、不覆盖、工作线程写盘、失败提示及换片后的旧结果丢弃。恢复检查故意制造字幕数字 ID 间隙，要求按指纹重选；同时核对最新倍速意图、延迟、字体、显隐、暂停位置和 A–B。M0 回归也直接验证生产恢复，不再由小样手动补回属性。

`zopana_m4 --recovery FIXTURES OUTPUT` 可单独复查信息与置顶，`--engine-recovery` 单独复查渲染重建。每个进程保持单个播放核心，与正式程序一致；三档均分别执行两组检查。早期双核心测试在退出清理时触发了本机 WeType DLL 卸载异常，拆开后正常退出；没有禁用或替换正式程序的输入法。真实 IME、物理键盘布局/拖放、多屏 DPI、长时间资源与音频听感留在 M6。`ZOPANA_BUILD_M4_PROBE` 默认关闭。

打包到新的 `dist-final` 后，使用 `experiments/m2/verify-package.py BUILD MEDIA_FILE` 核对部署；再运行 `python experiments/m4/evidence.py BUILD TEST_MEDIA_ROOT`，校验三档结果和四个长期素材库，并生成汇总与实际工作树 SHA-256 清单。工作树清单忽略已删除路径，包含尚未提交的新文件，不修改 Git 暂存状态。
