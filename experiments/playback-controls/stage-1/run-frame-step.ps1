param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [switch]$Reference,
    [switch]$RenderTimeout,
    [switch]$SlowSettings
)

$ErrorActionPreference = 'Stop'
if (([int]$Reference.IsPresent + [int]$RenderTimeout.IsPresent + [int]$SlowSettings.IsPresent) -gt 1) {
    throw 'Choose one focused frame-step mode.'
}
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixtures = (Resolve-Path -LiteralPath $FixtureDir).Path
$exe = (Resolve-Path -LiteralPath "$buildPath/zopana_playback_frames.exe").Path
$prefix = if ($Reference) { 'reference-' } elseif ($RenderTimeout) { 'render-timeout-' } elseif ($SlowSettings) { 'slow-settings-' } else { 'frame-step-' }
$runPath = Join-Path $buildPath ($prefix + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $runPath | Out-Null

function New-Fixture {
    & ffmpeg -hide_banner -loglevel error @args
    if ($LASTEXITCODE) { throw "FFmpeg failed to generate $($args[-1])" }
}

$testsrc = @('-f', 'lavfi', '-i', 'testsrc2=s=160x90:r=25:d=9')
$encode = @('-c:v', 'libx264', '-g', '150', '-bf', '2', '-sc_threshold', '0')
New-Fixture @testsrc @encode "$runPath/cfr.mkv"
New-Fixture @testsrc -vf "select='lt(t,3)+between(t,3,6)*not(mod(n,5))+gt(t,6)*not(mod(n,2))'" -fps_mode vfr @encode "$runPath/vfr.mkv"
$marker = "drawbox=x=312:y=160:w=16:h=12:c=lime:t=fill:enable='eq(n,25)',drawbox=x=300:y=180:w=16:h=12:c=blue:t=fill:enable='eq(n,26)'"
New-Fixture -f lavfi -i 'color=c=0x202020:s=640x360:r=25:d=3' -vf $marker @encode "$runPath/marker.mkv"
New-Fixture -i "$fixtures/audio.m4a" -i "$fixtures/grid.png" -map '0:a' -map '1:v' '-c:a' copy '-c:v' mjpeg `
    '-frames:v' 1 '-disposition:v:0' attached_pic "$runPath/audio-cover.m4a"

$media = [ordered]@{ cfr = "$runPath/cfr.mkv"; vfr = "$runPath/vfr.mkv"; '4k' = "$fixtures/motion-4k.mp4" }
foreach ($name in $media.Keys) {
    & ffprobe -v error -select_streams v:0 -show_entries frame=best_effort_timestamp_time -of json $media[$name] |
        Out-File -LiteralPath "$runPath/$name-frames.json" -Encoding utf8
    if ($LASTEXITCODE) { throw "FFprobe failed to read frames from $($media[$name])" }
}

$log = Join-Path $runPath 'probe.log'
Write-Output "Evidence: $runPath"
$validationLock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$validationLock.WaitOne(0)) { $validationLock.Dispose(); throw 'Another UI validation run is active.' }
$previousPath = $env:PATH
$previousPlatform = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $env:PATH = "$QtRoot/bin;$env:PATH"
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
    $probeArgs = @($fixtures, $runPath)
    if ($Reference) { $probeArgs += '--reference' }
    if ($RenderTimeout) { $probeArgs += '--render-timeout' }
    if ($SlowSettings) { $probeArgs += '--slow-settings' }
    & $exe @probeArgs 2>&1 | Out-File -LiteralPath $log -Encoding utf8
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath $log -Tail 65 -Encoding utf8
        throw "Frame step probe failed ($LASTEXITCODE)"
    }
    $checks = @(Select-String -LiteralPath $log -Pattern '^PASS ').Count
    $environment = (Select-String -LiteralPath $log -Pattern '^Qt ').Line
    if ($Reference) {
        Write-Output "PASS reference comparison ($checks checks, $environment)"
    } elseif ($RenderTimeout) {
        Write-Output "PASS render timeout recovery ($checks checks, $environment)"
        Select-String -LiteralPath $log -Pattern '^4K render stall ' | ForEach-Object Line
    } elseif ($SlowSettings) {
        Write-Output "PASS slow settings write ($checks checks, $environment)"
        Select-String -LiteralPath $log -Pattern '^4K |^First settings write held ' | ForEach-Object Line
        $restartLog = Join-Path $runPath 'restart.log'
        & $exe $fixtures $runPath --read-slow-settings 2>&1 | Out-File -LiteralPath $restartLog -Encoding utf8
        if ($LASTEXITCODE) {
            Get-Content -LiteralPath $restartLog -Tail 30 -Encoding utf8
            throw "Settings restart probe failed ($LASTEXITCODE)"
        }
        Write-Output 'PASS restart restores last settings'
    } else {
        Write-Output "PASS frame step ($checks checks, $environment)"
        Select-String -LiteralPath $log -Pattern '^4K ' | ForEach-Object Line
    }
} finally {
    $env:PATH = $previousPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $previousPlatform
    $validationLock.ReleaseMutex()
    $validationLock.Dispose()
}
