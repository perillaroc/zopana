param(
    [Parameter(Mandatory)][string]$BuildDir,
    [Parameter(Mandatory)][string]$FixtureDir,
    [Parameter(Mandatory)][string]$BaselineExe,
    [string]$ReportDir = $BuildDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [ValidateRange(0.1, 5.0)][double]$ScaleFactor = 1.0
)

$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$fixtures = (Resolve-Path -LiteralPath $FixtureDir).Path
$baseline = (Resolve-Path -LiteralPath $BaselineExe).Path
$reports = (New-Item -ItemType Directory -Path $ReportDir -Force).FullName
$lock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$lock.WaitOne(0)) { $lock.Dispose(); throw 'Another UI validation is active.' }
$env:PATH = "$QtRoot/bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
$env:QT_SCALE_FACTOR_ROUNDING_POLICY = 'PassThrough'
$env:QT_SCALE_FACTOR = $ScaleFactor.ToString([Globalization.CultureInfo]::InvariantCulture)

function Run-Case([string]$Exe, [string]$Mode, [string]$Name) {
    $output = Join-Path $reports $Name
    if (Test-Path -LiteralPath $output) { throw 'Choose a fresh report directory so every probe gets a fresh profile.' }
    & $Exe $Mode $fixtures $output 2>&1 | Out-File -Encoding utf8 "$output.log"
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath "$output.log" -Tail 20
        throw "$Name failed ($LASTEXITCODE)"
    }
    Write-Output "PASS $Name"
}

try {
    Get-FileHash -LiteralPath $baseline, "$build/zopana_m0_regression.exe", "$build/zopana_m6.exe" |
        ConvertTo-Json | Set-Content -LiteralPath "$reports/probe-binaries.json" -Encoding utf8
    Run-Case "$build/zopana_m6.exe" 'shortcuts' 'shortcuts'
    Run-Case "$build/zopana_m6.exe" 'stress' 'stress'
    foreach ($pair in 1..3) {
        if ($pair % 2 -eq 0) {
            Run-Case "$build/zopana_m0_regression.exe" 'production-perf' "performance-production-$pair"
            Run-Case $baseline 'baseline-perf' "performance-baseline-$pair"
        } else {
            Run-Case $baseline 'baseline-perf' "performance-baseline-$pair"
            Run-Case "$build/zopana_m0_regression.exe" 'production-perf' "performance-production-$pair"
        }
    }
} finally {
    $lock.ReleaseMutex()
    $lock.Dispose()
}
