# 播放控制阶段 1 验证

`run.ps1` 依次运行偏好重启、配置迁移及定位检查。脚本将日志、校验值、测试配置和媒体副本写入构建目录下每轮单独的证据目录。各探针使用独立配置与历史路径，可在真实用户设置存在时直接运行。

## 构建探针

在 VS 2022 x64 Developer PowerShell 7 中从源码仓库根目录运行。准备 [Windows x64 构建环境](../../../docs/development/build.md)、可打开 Qt/OpenGL 窗口的桌面、`zopana-ui-v1` 素材目录和位于其同级目录的 `zopana-ui-m2-v1/long-grid.mp4`。下面的路径适用于源码位于工作区 `repo/zopana` 时。按实际位置修改 `$qt`、`$mpv` 和 `$fixtures`。

```powershell
$qt = 'C:/lang/qt/6.11.3/msvc2022_64'
$mpv = (Resolve-Path '../../deps/libmpv/0.42.0-windows-x64').Path
$fixtures = '../../test-media/zopana-ui-v1'
$build = '../../build/playback-controls-stage-1-6'
cmake -S . -B $build -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$qt" "-DMPV_ROOT:PATH=$mpv" -DZOPANA_BUILD_PLAYBACK_CONTROLS_STAGE1_PROBE=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build $build --target zopana zopana_playback_preferences zopana_playback_settings zopana_playback_seek
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
```

## 准备测试素材

已有素材时可直接运行检查。需要从源码生成时，在新的目录中运行现有 M0、M2 生成器。需要 Python 3、Pillow 和带 `libx264` 的 FFmpeg。

```powershell
$fixtureRoot = Join-Path $build ('fixtures-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$fixtures = Join-Path $fixtureRoot 'zopana-ui-v1'
python ./experiments/m0/fixtures.py $fixtures
if ($LASTEXITCODE -ne 0) { throw 'Base fixture generation failed' }
python ./experiments/m2/fixtures.py (Join-Path $fixtureRoot 'zopana-ui-m2-v1') --base $fixtures
if ($LASTEXITCODE -ne 0) { throw 'Seek fixture generation failed' }
```

## 运行配置与定位检查

`NativeDpr` 取显示器的原生 Qt DPR，例如 Windows 缩放为 200% 时使用 `2`。偏好检查通过 Qt 进程缩放采样 DPR 1、1.5、2。配置和定位检查使用 DPR 1。运行时保持桌面解锁，让探针窗口获得焦点，并等待脚本结束后再操作窗口。

```powershell
$runner = './experiments/playback-controls/stage-1/run.ps1'
$nativeDpr = 2
& $runner -BuildDir $build -FixtureDir $fixtures -QtRoot $qt -NativeDpr $nativeDpr
```

| `-Checks` 选项 | 验收范围 | 证据目录前缀 |
| --- | --- | --- |
| `Preferences` | P01 中阶段 1 已接入的偏好。每档 DPR 启动写入和读取两个进程，检查关闭历史时的属性、置顶和取景恢复。 | `p01-` |
| `Settings` | P02–P03，schema 迁移、未知字段、有效旧按键、按页复位、历史隔离、写保护、已落盘设置在强制终止后恢复。 | `p02-p03-` |
| `Seek` | S01–S04，默认及重启后的步长、菜单与提示、连续定位、首尾限幅和按焦点分配按键。 | `s01-s04-` |

只重跑配置和定位时，在同一命令末尾加 `-Checks Settings,Seek`。每组开头输出证据目录，成功时显示对应进程的 `PASS` 和检查项数。任何一组报错都会让总入口失败，可从最后输出的目录查看日志。`Settings` 只在该轮证据目录设置写保护并恢复其权限，强制终止的是该轮启动的检查进程。

## 旋转与元数据小样

构建同一目录下的两个额外目标。准备 Python 3、FFprobe 和含 `libx264`、`libvpx-vp9` 的 FFmpeg。

```powershell
cmake --build $build --target zopana_playback_rotation zopana_playback_metadata
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$stage1 = './experiments/playback-controls/stage-1'
& "$stage1/run-rotation.ps1" -BuildDir $build -FixtureDir $fixtures -QtRoot $qt -NativeDpr $nativeDpr
& "$stage1/run-metadata.ps1" -BuildDir $build -FixtureDir $fixtures -QtRoot $qt
```

旋转小样输出三档 DPR 的取景截图和尺寸数据，证据目录以 `rotation-` 开头。元数据小样在前台播放时验证无输出读取和超时回收，证据目录以 `metadata-` 开头。
