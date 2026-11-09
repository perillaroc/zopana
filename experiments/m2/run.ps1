param(
    [string]$BuildDir = "$PSScriptRoot/../../../../build/ui-m2-release-20261109",
    [string]$FixtureDir = "$PSScriptRoot/../../../../test-media/zopana-ui-v1",
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64'
)
$ErrorActionPreference = 'Stop'
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
        throw "M2 $log failed ($LASTEXITCODE)"
    }
    Write-Output "PASS $log"
}
$env:QT_SCALE_FACTOR = '1'
Run-Case 'zopana_m0_regression' @('info', $fixturePath, "$buildPath/environment") 'environment'
$nativeDpr = (Get-Content -Raw -Encoding utf8 -LiteralPath "$buildPath/environment/results.json" | ConvertFrom-Json).details.devicePixelRatio
foreach ($percent in @(100, 150, 200)) {
    $env:QT_SCALE_FACTOR = (($percent / 100.0) / $nativeDpr).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
    Run-Case 'zopana_m2' @($fixturePath, "$buildPath/m2-$percent") "m2-$percent"
    Run-Case 'zopana_m1' @($fixturePath, "$buildPath/qa-$percent") "qa-$percent"
    Run-Case 'zopana_m0_regression' @('probe', $fixturePath, "$buildPath/m0-regression/probe-$percent") "m0-$percent"
}
& python "$PSScriptRoot/../m0/analyze.py" "$buildPath/m0-regression"
if ($LASTEXITCODE) { throw 'M0 pixel regression failed' }
& python "$PSScriptRoot/../m1/analyze.py" $buildPath
if ($LASTEXITCODE) { throw 'M1 asset and contrast checks failed' }
Write-Output "Evidence: $buildPath"
