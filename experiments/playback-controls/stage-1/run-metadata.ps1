param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64'
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixturePath = (Resolve-Path -LiteralPath $FixtureDir).Path
$exe = (Resolve-Path -LiteralPath "$buildPath/zopana_playback_metadata.exe").Path
$env:PATH = "$QtRoot/bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
$runPath = Join-Path $buildPath ('metadata-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $runPath | Out-Null
Write-Output "Evidence: $runPath"
& python "$PSScriptRoot/metadata-fixtures.py" $fixturePath "$runPath/media"
if ($LASTEXITCODE) { throw "Fixture generation failed ($LASTEXITCODE)" }
Get-FileHash -Algorithm SHA256 -LiteralPath $exe, "$buildPath/libmpv-2.dll" |
    ConvertTo-Json | Set-Content -LiteralPath "$runPath/build-hashes.json" -Encoding utf8

$validationLock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$validationLock.WaitOne(0)) { $validationLock.Dispose(); throw 'Another UI validation run is active.' }
try {
    $log = Join-Path $runPath 'probe.log'
    & $exe "$runPath/media" $runPath 2> "$runPath/player.log" | Out-File -LiteralPath $log -Encoding utf8
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath $log -Tail 20 -Encoding utf8
        throw "Metadata probe failed ($LASTEXITCODE)"
    }
    $checks = @(Select-String -LiteralPath $log -Pattern '^PASS ').Count
    Write-Output "PASS metadata ($checks checks)"
} finally {
    $validationLock.ReleaseMutex()
    $validationLock.Dispose()
}
