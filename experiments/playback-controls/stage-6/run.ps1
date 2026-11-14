#requires -version 7.0
param(
    [Parameter(Mandatory)][string]$BuildDir,
    [Parameter(Mandatory)][string]$FixtureDir,
    [Parameter(Mandatory)][string]$ReportDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [ValidateScript({ $_ -gt 0 })][double]$NativeDpr = 2
)

$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$fixtures = (Resolve-Path -LiteralPath $FixtureDir).Path
$long = (Resolve-Path -LiteralPath (Join-Path $fixtures '../zopana-ui-m2-v1/long-grid.mp4')).Path
$exe = (Resolve-Path -LiteralPath "$build/zopana_playback_release.exe").Path
$out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportDir)
if (Test-Path -LiteralPath $out) { throw 'Choose a fresh report directory.' }
$media = Join-Path $out 'media'
$null = New-Item -ItemType Directory -Path $media
Copy-Item -LiteralPath "$fixtures/网格 multi.mkv","$fixtures/换片 C.mkv","$fixtures/audio.m4a" -Destination $media
function New-Fixture([string[]]$MediaArguments) {
    & ffmpeg.exe -nostdin -hide_banner -loglevel error -n @MediaArguments
    if ($LASTEXITCODE) { throw 'Could not create the test fixture.' }
}
New-Fixture @('-i',$long,'-map','0','-c','copy','-metadata','title=集成测试 A',"$media/长片 A.mp4")
New-Fixture @('-i',$long,'-map','0','-c','copy','-metadata','title=集成测试 B','-t','113',"$media/长片 B.mp4")
New-Fixture @('-i',"$fixtures/网格 multi.mkv",'-map','0:v:0','-map','0:a:1','-map','0:a:0','-map','0:s:1','-map','0:s:0','-c','copy',"$media/轨道重排.mkv")
$files = @($exe, "$build/zopana.exe", "$build/libmpv-2.dll") + (Get-ChildItem -LiteralPath $media -File).FullName
Get-FileHash -LiteralPath $files | ConvertTo-Json | Set-Content -LiteralPath "$out/hashes.json" -Encoding utf8

$variables = 'PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_SCALE_FACTOR_ROUNDING_POLICY', 'QT_SCALE_FACTOR'
$saved = @{}
foreach ($var in $variables) { $saved[$var] = [Environment]::GetEnvironmentVariable($var) }
$mutex = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$mutex.WaitOne(0)) { $mutex.Dispose(); throw 'Another UI validation run is active.' }
try {
    $env:PATH = "$QtRoot/bin;$env:PATH"
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
    $env:QT_SCALE_FACTOR_ROUNDING_POLICY = 'PassThrough'
    $env:QT_SCALE_FACTOR = (1 / $NativeDpr).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
    foreach ($mode in 'integration','write','read','stress') {
        $log = "$out/$mode.log"
        & $exe $mode $media $out 2>&1 | Out-File -LiteralPath $log -Encoding utf8
        if ($LASTEXITCODE) {
            Get-Content -LiteralPath $log -Tail 30
            throw "Integrated playback probe failed: $mode."
        }
        $result = Get-Content -LiteralPath "$out/$mode.json" -Raw | ConvertFrom-Json
        if (!$result.passed) { throw "Integrated playback probe did not report success: $mode." }
        Write-Output "PASS $mode ($($result.checks.Count) checks)"
    }
    Write-Output "Evidence: $out"
} finally {
    $mutex.ReleaseMutex()
    $mutex.Dispose()
    foreach ($var in $variables) { [Environment]::SetEnvironmentVariable($var, $saved[$var], 'Process') }
}
