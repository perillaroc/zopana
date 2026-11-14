param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [ValidateScript({ $_ -gt 0 })] [double]$NativeDpr = 1
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixturePath = (Resolve-Path -LiteralPath $FixtureDir).Path
$exe = (Resolve-Path -LiteralPath "$buildPath/zopana_playback_repeat.exe").Path
$runPath = Join-Path $buildPath ('repeat-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$media = Join-Path $runPath 'media'
New-Item -ItemType Directory -Path $media | Out-Null
foreach ($name in 'E1.mp4', 'E2.mp4', 'E10.mp4') {
    Copy-Item -LiteralPath (Join-Path $fixturePath $name) -Destination $media
}
Get-FileHash -Algorithm SHA256 -LiteralPath (@($exe, "$buildPath/libmpv-2.dll") + (Get-ChildItem -LiteralPath $media -File).FullName) |
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
    $log = Join-Path $runPath 'probe.log'
    & $exe $media $runPath 2>&1 | Out-File -LiteralPath $log -Encoding utf8
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath $log -Tail 30 -Encoding utf8
        throw "Repeat probe failed ($LASTEXITCODE)"
    }
    $checks = @(Select-String -LiteralPath $log -Pattern '^PASS ').Count
    Write-Output "PASS repeat ($checks checks)"
} finally {
    $validationLock.ReleaseMutex()
    $validationLock.Dispose()
    foreach ($name in $savedEnvironment.Keys) {
        Set-Item -LiteralPath "Env:$name" -Value $savedEnvironment[$name]
    }
}
