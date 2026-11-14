param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [ValidateScript({ $_ -gt 0 })] [double]$NativeDpr = 1
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixturePath = (Resolve-Path -LiteralPath $FixtureDir).Path
$exe = (Resolve-Path -LiteralPath "$buildPath/zopana_playback_preferences.exe").Path
$env:PATH = "$QtRoot/bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
$env:QT_SCALE_FACTOR_ROUNDING_POLICY = 'PassThrough'
$runPath = Join-Path $buildPath ('p01-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$mediaPath = Join-Path $runPath 'media'
New-Item -ItemType Directory -Path $mediaPath | Out-Null
foreach ($name in '网格 multi.mkv','portrait.mp4','motion-1080.mp4','audio.m4a') {
    Copy-Item -LiteralPath (Join-Path $fixturePath $name) -Destination $mediaPath
}
Get-ChildItem -LiteralPath $mediaPath -File | Get-FileHash -Algorithm SHA256 | ConvertTo-Json |
    Set-Content -LiteralPath "$runPath/fixture-hashes.json" -Encoding utf8
Get-FileHash -Algorithm SHA256 -LiteralPath $exe, "$buildPath/zopana.exe", "$buildPath/libmpv-2.dll" | ConvertTo-Json |
    Set-Content -LiteralPath "$runPath/build-hashes.json" -Encoding utf8
Write-Output "Evidence: $runPath"

$validationLock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$validationLock.WaitOne(0)) { $validationLock.Dispose(); throw 'Another UI validation run is active.' }
try {
    foreach ($percent in 100, 150, 200) {
        $env:QT_SCALE_FACTOR = (($percent / 100.0) / $NativeDpr).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
        $profilePath = Join-Path $runPath "dpr-$percent"
        foreach ($mode in 'write', 'read') {
            $log = Join-Path $runPath "dpr-$percent-$mode.log"
            & $exe $mode $mediaPath $profilePath 2>&1 | Out-File -LiteralPath $log -Encoding utf8
            if ($LASTEXITCODE) {
                Get-Content -LiteralPath $log -Tail 30 -Encoding utf8
                throw "P01 $percent $mode failed ($LASTEXITCODE)"
            }
            $checks = @(Select-String -LiteralPath $log -Pattern '^PASS ').Count
            $environment = (Select-String -LiteralPath $log -Pattern '^Qt ').Line
            Write-Output "PASS $percent $mode ($checks checks, $environment)"
            if ($mode -eq 'write') {
                Copy-Item -LiteralPath "$profilePath/config/settings.ini" -Destination "$profilePath/saved-before-restart.ini"
            }
        }
    }
} finally {
    $validationLock.ReleaseMutex()
    $validationLock.Dispose()
}
