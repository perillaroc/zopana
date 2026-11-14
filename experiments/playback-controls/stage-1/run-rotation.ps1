param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [ValidateScript({ $_ -gt 0 })] [double]$NativeDpr = 1
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixturePath = (Resolve-Path -LiteralPath $FixtureDir).Path
$exe = (Resolve-Path -LiteralPath "$buildPath/zopana_playback_rotation.exe").Path
$env:PATH = "$QtRoot/bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
$env:QT_SCALE_FACTOR_ROUNDING_POLICY = 'PassThrough'
$runPath = Join-Path $buildPath ('rotation-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $runPath | Out-Null
Write-Output "Evidence: $runPath"
& python "$PSScriptRoot/rotation-fixtures.py" $fixturePath "$runPath/media"
if ($LASTEXITCODE) { throw "Fixture generation failed ($LASTEXITCODE)" }
Get-FileHash -Algorithm SHA256 -LiteralPath $exe, "$buildPath/libmpv-2.dll" |
    ConvertTo-Json | Set-Content -LiteralPath "$runPath/build-hashes.json" -Encoding utf8

$validationLock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$validationLock.WaitOne(0)) { $validationLock.Dispose(); throw 'Another UI validation run is active.' }
try {
    foreach ($percent in 100, 150, 200) {
        $dpr = ($percent / 100.0).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
        $env:QT_SCALE_FACTOR = (($percent / 100.0) / $NativeDpr).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
        $log = Join-Path $runPath "dpr-$percent.log"
        & $exe "$runPath/media" "$runPath/dpr-$percent" $dpr 2>&1 | Out-File -LiteralPath $log -Encoding utf8
        if ($LASTEXITCODE) {
            Get-Content -LiteralPath $log -Tail 35 -Encoding utf8
            throw "Rotation probe failed at DPR $dpr ($LASTEXITCODE)"
        }
        $summary = Get-Content -LiteralPath "$runPath/dpr-$percent/results.json" -Raw | ConvertFrom-Json
        if (!$summary.passed) { throw "Rotation probe has no passing result at DPR $dpr" }
        $checks = @(Select-String -LiteralPath $log -Pattern '^PASS ').Count
        Write-Output "PASS DPR $dpr ($checks checks)"
        $restartLog = Join-Path $runPath "restart-$percent.log"
        & $exe "$runPath/media" "$runPath/dpr-$percent" $dpr --read 2>&1 | Out-File -LiteralPath $restartLog -Encoding utf8
        if ($LASTEXITCODE) {
            Get-Content -LiteralPath $restartLog -Tail 35 -Encoding utf8
            throw "Rotation restart probe failed at DPR $dpr ($LASTEXITCODE)"
        }
        $restart = Get-Content -LiteralPath "$runPath/dpr-$percent/restart.json" -Raw | ConvertFrom-Json
        if (!$restart.passed) { throw "Rotation restart probe has no passing result at DPR $dpr" }
        $checks = @(Select-String -LiteralPath $restartLog -Pattern '^PASS ').Count
        Write-Output "PASS restart DPR $dpr ($checks checks)"
    }
} finally {
    $validationLock.ReleaseMutex()
    $validationLock.Dispose()
}
