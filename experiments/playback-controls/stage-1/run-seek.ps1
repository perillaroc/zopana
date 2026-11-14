param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [ValidateScript({ $_ -gt 0 })] [double]$NativeDpr = 1
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixtures = (Resolve-Path -LiteralPath $FixtureDir).Path
$exe = (Resolve-Path -LiteralPath "$buildPath/zopana_playback_seek.exe").Path
$env:PATH = "$QtRoot/bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
$env:QT_SCALE_FACTOR_ROUNDING_POLICY = 'PassThrough'
$env:QT_SCALE_FACTOR = (1 / $NativeDpr).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
$runPath = Join-Path $buildPath ('s01-s04-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$media = Join-Path $runPath 'media'
$profile = Join-Path $runPath 'profile'
New-Item -ItemType Directory -Path $media, $profile | Out-Null
Copy-Item -LiteralPath "$fixtures/audio.m4a", "$fixtures/grid.png", "$fixtures/broken.mkv", "$fixtures/../zopana-ui-m2-v1/long-grid.mp4" -Destination $media
$files = (Get-ChildItem -LiteralPath $media -File).FullName
Get-FileHash -Algorithm SHA256 -LiteralPath (@($exe, "$buildPath/zopana.exe", "$buildPath/libmpv-2.dll") + $files) |
    ConvertTo-Json | Set-Content -LiteralPath "$runPath/hashes.json" -Encoding utf8
Write-Output "Evidence: $runPath"

$validationLock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$validationLock.WaitOne(0)) { $validationLock.Dispose(); throw 'Another UI validation run is active.' }
try {
    foreach ($mode in 'write', 'read') {
        $log = Join-Path $runPath "$mode.log"
        & $exe $mode $media $profile 2>&1 | Out-File -LiteralPath $log -Encoding utf8
        if ($LASTEXITCODE) {
            Get-Content -LiteralPath $log -Tail 35 -Encoding utf8
            throw "Seek probe $mode failed ($LASTEXITCODE)"
        }
        $checks = @(Select-String -LiteralPath $log -Pattern '^PASS ').Count
        $environment = (Select-String -LiteralPath $log -Pattern '^Qt ').Line
        Write-Output "PASS $mode ($checks checks, $environment)"
        if ($mode -eq 'write') {
            Copy-Item -LiteralPath "$profile/config/settings.ini" -Destination "$runPath/settings-before-restart.ini"
            Copy-Item -LiteralPath "$profile/seek-help.png" -Destination "$runPath/seek-help-in-session.png"
        }
    }
} finally {
    $validationLock.ReleaseMutex()
    $validationLock.Dispose()
}
