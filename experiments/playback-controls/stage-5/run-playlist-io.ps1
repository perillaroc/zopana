param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64'
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixtures = (Resolve-Path -LiteralPath $FixtureDir).Path
$exe = (Resolve-Path -LiteralPath "$buildPath/zopana_playlist_io.exe").Path
$runPath = Join-Path $buildPath ('playlist-io-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $runPath | Out-Null
Write-Output "Evidence: $runPath"

$validationLock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$validationLock.WaitOne(0)) { $validationLock.Dispose(); throw 'Another UI validation run is active.' }
$previousPath = $env:PATH
$previousPlatform = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $env:PATH = "$QtRoot/bin;$env:PATH"
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
    foreach ($mode in 'write', 'read') {
        $log = Join-Path $runPath "$mode.log"
        & $exe $mode $fixtures $runPath 2>&1 | Out-File -LiteralPath $log -Encoding utf8
        if ($LASTEXITCODE) {
            Get-Content -LiteralPath $log -Tail 45 -Encoding utf8
            throw "Playlist probe $mode failed ($LASTEXITCODE)"
        }
        $checks = @(Select-String -LiteralPath $log -Pattern '^PASS ').Count
        $environment = (Select-String -LiteralPath $log -Pattern '^Qt ').Line
        Write-Output "PASS playlist $mode ($checks checks, $environment)"
    }
} finally {
    $env:PATH = $previousPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $previousPlatform
    $validationLock.ReleaseMutex()
    $validationLock.Dispose()
}
