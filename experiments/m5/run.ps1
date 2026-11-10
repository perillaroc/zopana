param(
    [string]$BuildDir = "$PSScriptRoot/../../../../build/ui-m5-release-20261110",
    [string]$FixtureDir = "$PSScriptRoot/../../../../test-media/zopana-ui-v1",
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64'
)
$ErrorActionPreference = 'Stop'
$validationLock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$validationLock.WaitOne(0)) { $validationLock.Dispose(); throw 'Another UI validation run is active; run these window tests sequentially.' }
try {
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixturePath = (Resolve-Path -LiteralPath $FixtureDir).Path
$env:PATH = "$QtRoot/bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
$env:QT_SCALE_FACTOR_ROUNDING_POLICY = 'PassThrough'
$env:PYTHONUTF8 = '1'
function Run-Case([string]$exe, [string[]]$arguments, [string]$log) {
    & "$buildPath/$exe.exe" @arguments 2>&1 | Out-File -Encoding utf8 "$buildPath/$log.log"
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath "$buildPath/$log.log" -Tail 15
        throw "M5 $log failed ($LASTEXITCODE)"
    }
    Write-Output "PASS $log"
}
$env:QT_SCALE_FACTOR = '1'
Run-Case 'zopana_m0_regression' @('info', $fixturePath, "$buildPath/environment") 'environment'
$nativeDpr = (Get-Content -Raw -Encoding utf8 -LiteralPath "$buildPath/environment/results.json" | ConvertFrom-Json).details.devicePixelRatio
foreach ($percent in @(100, 150, 200)) {
    $env:QT_SCALE_FACTOR = (($percent / 100.0) / $nativeDpr).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
    Run-Case 'zopana_m5' @('ui', $fixturePath, "$buildPath/m5-$percent") "m5-$percent"
    Run-Case 'zopana_m5' @('write', $fixturePath, "$buildPath/roundtrip-$percent") "write-$percent"
    Copy-Item -LiteralPath "$buildPath/roundtrip-$percent/results.json" -Destination "$buildPath/roundtrip-$percent/write-results.json"
    Run-Case 'zopana_m5' @('read', $fixturePath, "$buildPath/roundtrip-$percent") "read-$percent"
    Copy-Item -LiteralPath "$buildPath/roundtrip-$percent/results.json" -Destination "$buildPath/roundtrip-$percent/read-results.json"
    Run-Case 'zopana_m4' @($fixturePath, "$buildPath/m4-$percent") "m4-$percent"
    Run-Case 'zopana_m4' @('--engine-recovery', $fixturePath, "$buildPath/m4-recovery-$percent") "m4-recovery-$percent"
    Run-Case 'zopana_m3' @($fixturePath, "$buildPath/m3-$percent") "m3-$percent"
    Run-Case 'zopana_m2' @($fixturePath, "$buildPath/m2-$percent") "m2-$percent"
    Run-Case 'zopana_m1' @($fixturePath, "$buildPath/qa-$percent") "qa-$percent"
    Run-Case 'zopana_m0_regression' @('probe', $fixturePath, "$buildPath/m0-regression/probe-$percent") "m0-$percent"
}
$env:QT_SCALE_FACTOR = (2.0 / $nativeDpr).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
Run-Case 'zopana_m5' @('read', $fixturePath, "$buildPath/roundtrip-100") 'cross-dpi'
Copy-Item -LiteralPath "$buildPath/roundtrip-100/results.json" -Destination "$buildPath/cross-dpi.json"
Copy-Item -LiteralPath "$buildPath/roundtrip-100/read-results.json" -Destination "$buildPath/roundtrip-100/results.json"
$env:QT_SCALE_FACTOR = (1.0 / $nativeDpr).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
Run-Case 'zopana_m5' @('stores', $fixturePath, "$buildPath/stores") 'stores'
& "$PSScriptRoot/abrupt-exit.ps1" -BuildDir $buildPath -FixtureDir $fixturePath
Run-Case 'zopana_m3' @('--lifecycle', $fixturePath, "$buildPath/lifecycle") 'lifecycle'
& python "$PSScriptRoot/../m0/analyze.py" "$buildPath/m0-regression"
if ($LASTEXITCODE) { throw 'M0 pixel regression failed' }
& python "$PSScriptRoot/../m1/analyze.py" $buildPath
if ($LASTEXITCODE) { throw 'Asset and contrast checks failed' }
Write-Output "Evidence: $buildPath"
} finally { $validationLock.ReleaseMutex(); $validationLock.Dispose() }
