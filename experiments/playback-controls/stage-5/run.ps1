param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [ValidateScript({ $_ -gt 0 })] [double]$NativeDpr = 2,
    [ValidateSet('1','1.5','2')] [string[]]$Dpr = @('1','1.5','2'),
    [ValidateSet('comparators','ui','write','read','throughput','recovery')]
    [string[]]$Checks = @('comparators','ui','write','read','throughput','recovery')
)

$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$fixtures = (Resolve-Path -LiteralPath $FixtureDir).Path
$exe = (Resolve-Path -LiteralPath "$build/zopana_playlist_sort.exe").Path
$app = (Resolve-Path -LiteralPath "$build/zopana.exe").Path
$long = (Resolve-Path -LiteralPath "$fixtures/../zopana-ui-m2-v1/long-grid.mp4").Path
$bulk = (Resolve-Path -LiteralPath "$fixtures/../zopana-ui-m3-v1/large").Path
$run = Join-Path $build ('sort-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$media = Join-Path $run 'media'
New-Item -ItemType Directory -Path $run | Out-Null
& python "$PSScriptRoot/../stage-1/metadata-fixtures.py" $fixtures $media
if ($LASTEXITCODE) { throw 'Could not generate metadata fixtures' }
Copy-Item -LiteralPath $long -Destination "$media/playing-long.mp4"
Copy-Item -LiteralPath "$fixtures/external.srt" -Destination $media
$tagged = Join-Path $media '未播放 第02集.mp4'
$short = Join-Path $media 'new-version.mp4'
& ffmpeg -hide_banner -loglevel error -nostdin -n -i $tagged -t 3 -map 0 -c copy -metadata 'title=Replacement title' $short
if ($LASTEXITCODE) { throw 'Could not generate the replacement fixture' }
if ($Checks -contains 'throughput') {
    New-Item -ItemType Directory -Path "$media/bulk" | Out-Null
    Get-ChildItem -LiteralPath $bulk -File -Filter '*.mp4' | Sort-Object Name | Select-Object -First 999 | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination "$media/bulk"
    }
}
$sources = @($exe, $app, "$build/libmpv-2.dll") + (Get-ChildItem -LiteralPath $media -Recurse -File).FullName
Get-FileHash -Algorithm SHA256 -LiteralPath $sources | ConvertTo-Json | Set-Content -LiteralPath "$run/hashes.json" -Encoding utf8
Write-Output "Evidence: $run"

$saved = @{}
foreach ($name in 'PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_SCALE_FACTOR_ROUNDING_POLICY', 'QT_SCALE_FACTOR') {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name)
}
$lock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$lock.WaitOne(0)) { $lock.Dispose(); throw 'Another UI validation run is active.' }
try {
    $env:PATH = "$QtRoot/bin;$env:PATH"
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
    $env:QT_SCALE_FACTOR_ROUNDING_POLICY = 'PassThrough'
    $culture = [Globalization.CultureInfo]::InvariantCulture
    for ($index = 0; $index -lt $Dpr.Count; ++$index) {
        $scale = $Dpr[$index]
        $env:QT_SCALE_FACTOR = ([double]::Parse($scale, $culture) / $NativeDpr).ToString('G17', $culture)
        foreach ($mode in $Checks) {
            if ($index -and $mode -ne 'ui') { continue }
            $output = Join-Path $run "$scale-$mode"
            if ($mode -in 'write', 'read') { $output = Join-Path $run "$scale-preferences" }
            New-Item -ItemType Directory -Path $output -Force | Out-Null
            if ($mode -eq 'ui') { Copy-Item -LiteralPath $tagged -Destination "$media/replacement.mp4" -Force }
            $log = Join-Path $output "$mode.log"
            & $exe $mode $media $output 2>&1 | Out-File -LiteralPath $log -Encoding utf8
            if ($LASTEXITCODE) {
                Get-Content -LiteralPath $log -Tail 70 -Encoding utf8
                throw "Stage 5 $mode at DPR $scale failed ($LASTEXITCODE)."
            }
            $result = Get-Content -LiteralPath "$output/$mode.json" -Raw -Encoding utf8 | ConvertFrom-Json
            if (!$result.passed) { throw "Stage 5 $mode did not report success." }
            Write-Output "PASS $mode at DPR $scale ($($result.checks.Count) checks)"
        }
    }

    # Smoke-test the production entry with pipes instead of a console, just as QProcess launches it.
    $startup = [Diagnostics.ProcessStartInfo]::new($app, '--playlist-metadata-worker')
    $startup.UseShellExecute = $false
    $startup.RedirectStandardInput = $true
    $startup.RedirectStandardOutput = $true
    $startup.StandardInputEncoding = [Text.UTF8Encoding]::new($false)
    $startup.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $worker = [Diagnostics.Process]::Start($startup)
    try {
        $worker.StandardInput.WriteLine((@{path = $tagged; media = $true} | ConvertTo-Json -Compress))
        $worker.StandardInput.Close()
        if (!$worker.WaitForExit(5000)) { $worker.Kill(); throw 'Production metadata worker timed out' }
        $reply = $worker.StandardOutput.ReadToEnd()
        $reply | Set-Content -LiteralPath "$run/production-worker.json" -Encoding utf8
        $metadata = ($reply | ConvertFrom-Json).media
        if ($worker.ExitCode -ne 0 -or !$metadata.read -or $metadata.title -ne '测试媒体标题' -or $metadata.width -ne 960 -or $metadata.height -ne 540) {
            throw 'Production metadata worker returned the wrong fields'
        }
        Write-Output 'PASS production metadata worker'
    } finally {
        $worker.Dispose()
    }
} finally {
    $lock.ReleaseMutex()
    $lock.Dispose()
    foreach ($name in $saved.Keys) {
        [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process')
    }
}
