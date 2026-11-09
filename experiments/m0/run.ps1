param(
    [string]$BuildDir = "$PSScriptRoot/../../../../build/ui-m0-20261109",
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
function Run-Case([string]$mode, [double]$factor, [string]$name) {
    $env:QT_SCALE_FACTOR = $factor.ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
    & "$buildPath/zopana_m0.exe" $mode $fixturePath "$buildPath/$name" 2>&1 | Out-File -Encoding utf8 "$buildPath/$name.log"
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath "$buildPath/$name.log" -Tail 20
        throw "M0 $name failed ($LASTEXITCODE)"
    }
    Write-Output "PASS $name"
}
Run-Case 'info' 1 'environment'
$nativeDpr = (Get-Content -Raw -Encoding utf8 -LiteralPath "$buildPath/environment/results.json" | ConvertFrom-Json).details.devicePixelRatio
foreach ($percent in @(100, 150, 200)) {
    $factor = ($percent / 100.0) / $nativeDpr
    $baselineMode = if ($percent -eq 100) { 'baseline-perf' } else { 'baseline' }
    Run-Case $baselineMode $factor "baseline-$percent"
    Run-Case 'probe' $factor "probe-$percent"
}
& python "$PSScriptRoot/analyze.py" $buildPath
if ($LASTEXITCODE) { throw 'M0 image analysis failed' }
$identities = Get-ChildItem -LiteralPath $PSScriptRoot -File | Get-FileHash -Algorithm SHA256
$identities | Select-Object Path,Hash | ConvertTo-Json | Set-Content -Encoding utf8 "$buildPath/probe-source-hashes.json"
Write-Output "Evidence: $buildPath"
