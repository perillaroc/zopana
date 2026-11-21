param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64'
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$exe = (Resolve-Path -LiteralPath "$buildPath/zopana_playback_navigation.exe").Path
$runPath = Join-Path $buildPath ('navigation-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$media = Join-Path $runPath 'media'
New-Item -ItemType Directory -Path $media | Out-Null

@'
;FFMETADATA1
[CHAPTER]
TIMEBASE=1/1000
START=0
END=2000
title=开场 & 引子
[CHAPTER]
TIMEBASE=1/1000
START=2000
END=5400
[CHAPTER]
TIMEBASE=1/1000
START=5400
END=9000
title=复核
[CHAPTER]
TIMEBASE=1/1000
START=10000
END=12000
title=超出时长
'@ | Set-Content -LiteralPath "$media/chapters.ffmetadata" -Encoding utf8
& ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=s=160x90:r=25:d=9' `
    -c:v libx264 -g 150 -bf 2 -sc_threshold 0 "$media/no-chapters.mkv"
if ($LASTEXITCODE) { throw 'Could not generate the video fixture.' }
& ffmpeg -hide_banner -loglevel error -i "$media/no-chapters.mkv" -i "$media/chapters.ffmetadata" `
    -map 0:v -map_chapters 1 -c copy "$media/chapters.mkv"
if ($LASTEXITCODE) { throw 'Could not add chapters to the video fixture.' }

$log = Join-Path $runPath 'probe.log'
Write-Output "Evidence: $runPath"
$validationLock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$validationLock.WaitOne(0)) { $validationLock.Dispose(); throw 'Another UI validation run is active.' }
$previousPath = $env:PATH
$previousPlatform = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $env:PATH = "$QtRoot/bin;$env:PATH"
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
    & $exe $media $runPath 2>&1 | Out-File -LiteralPath $log -Encoding utf8
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath $log -Tail 45 -Encoding utf8
        throw "Navigation probe failed ($LASTEXITCODE)"
    }
    $checks = @(Select-String -LiteralPath $log -Pattern '^PASS ').Count
    $environment = (Select-String -LiteralPath $log -Pattern '^Qt ').Line
    Write-Output "PASS navigation ($checks checks, $environment)"
} finally {
    $env:PATH = $previousPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $previousPlatform
    $validationLock.ReleaseMutex()
    $validationLock.Dispose()
}
