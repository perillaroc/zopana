param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [ValidateScript({ $_ -gt 0 })] [double]$NativeDpr = 1,
    [ValidateSet('ui','start','queue','manual','timing','read')]
    [string[]]$Checks = @('ui','read','start','queue','manual','timing')
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixturePath = (Resolve-Path -LiteralPath $FixtureDir).Path
$exe = (Resolve-Path -LiteralPath "$buildPath/zopana_playback_skip.exe").Path
$runPath = Join-Path $buildPath ('skip-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$media = Join-Path $runPath 'media'
New-Item -ItemType Directory -Path $media | Out-Null
$source = (Resolve-Path -LiteralPath (Join-Path $fixturePath '../zopana-ui-m2-v1/long-grid.mp4')).Path
$ffmpeg = (Get-Command ffmpeg -CommandType Application).Source
& $ffmpeg -hide_banner -loglevel error -nostdin -i $source -t 120 -map 0:v:0 -map 0:a:0 -c copy "$media/long.mp4"
if ($LASTEXITCODE) { throw 'Could not prepare the 120-second video.' }
& $ffmpeg -hide_banner -loglevel error -nostdin -i $source -t 20 -map 0:v:0 -map 0:a:0 -c copy "$media/short.mp4"
if ($LASTEXITCODE) { throw 'Could not prepare the 20-second video.' }
& $ffmpeg -hide_banner -loglevel error -nostdin -i $source -t 20 -vn -c:a copy "$media/audio.m4a"
if ($LASTEXITCODE) { throw 'Could not prepare the audio fixture.' }
Copy-Item -LiteralPath (Join-Path $fixturePath 'E1.mp4') -Destination $media
Copy-Item -LiteralPath (Join-Path $fixturePath 'E2.mp4') -Destination $media
Copy-Item -LiteralPath (Join-Path $fixturePath 'bad1.mkv') -Destination $media
Get-FileHash -LiteralPath (@($exe, "$buildPath/zopana.exe", "$buildPath/libmpv-2.dll", $source) + (Get-ChildItem -LiteralPath $media -File).FullName) |
    ConvertTo-Json | Set-Content -LiteralPath "$runPath/hashes.json" -Encoding utf8
Write-Output "Evidence: $runPath"

$savedEnvironment = @{}
foreach ($name in 'PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_SCALE_FACTOR_ROUNDING_POLICY', 'QT_SCALE_FACTOR') {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name)
}
$validationLock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$validationLock.WaitOne(0)) { $validationLock.Dispose(); throw 'Another UI validation run is active.' }
try {
    $env:PATH = "$QtRoot/bin;$env:PATH"
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
    $env:QT_SCALE_FACTOR_ROUNDING_POLICY = 'PassThrough'
    $env:QT_SCALE_FACTOR = (1 / $NativeDpr).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
    foreach ($mode in $Checks) {
        $log = Join-Path $runPath "$mode.log"
        & $exe $mode $media $runPath 2>&1 | Out-File -LiteralPath $log -Encoding utf8
        if ($LASTEXITCODE) {
            Get-Content -LiteralPath $log -Tail 45 -Encoding utf8
            throw "Playback skip probe $mode failed ($LASTEXITCODE)."
        }
        $count = @(Select-String -LiteralPath $log -Pattern '^PASS ').Count
        Write-Output "PASS skip $mode ($count checks)"
    }
} finally {
    $validationLock.ReleaseMutex()
    $validationLock.Dispose()
    foreach ($name in $savedEnvironment.Keys) {
        Set-Item -LiteralPath "Env:$name" -Value $savedEnvironment[$name]
    }
}
