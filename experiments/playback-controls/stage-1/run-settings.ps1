param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [ValidateScript({ $_ -gt 0 })] [double]$NativeDpr = 1
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixture = Join-Path (Resolve-Path -LiteralPath $FixtureDir).Path 'grid.mp4'
$exe = (Resolve-Path -LiteralPath "$buildPath/zopana_playback_settings.exe").Path
$env:PATH = "$QtRoot/bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$QtRoot/plugins/platforms"
$env:QT_SCALE_FACTOR_ROUNDING_POLICY = 'PassThrough'
$env:QT_SCALE_FACTOR = (1 / $NativeDpr).ToString('G17', [Globalization.CultureInfo]::InvariantCulture)
$runPath = Join-Path $buildPath ('p02-p03-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $runPath | Out-Null
$media = Join-Path $runPath 'grid.mp4'
Copy-Item -LiteralPath $fixture -Destination $media
Get-FileHash -Algorithm SHA256 -LiteralPath $exe, "$buildPath/zopana.exe", "$buildPath/libmpv-2.dll", $media |
    ConvertTo-Json | Set-Content -LiteralPath "$runPath/hashes.json" -Encoding utf8
Write-Output "Evidence: $runPath"

function Invoke-Probe([string]$mode, [string]$profile) {
    $log = Join-Path $runPath "$mode.log"
    & $exe $mode $media $profile 2>&1 | Out-File -LiteralPath $log -Encoding utf8
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath $log -Tail 30 -Encoding utf8
        throw "Settings probe $mode failed ($LASTEXITCODE)"
    }
    $checks = @(Select-String -LiteralPath $log -Pattern '^PASS ').Count
    Write-Output "PASS $mode ($checks checks)"
}

$validationLock = [Threading.Mutex]::new($false, 'Local\zopana-ui-validation')
if (!$validationLock.WaitOne(0)) { $validationLock.Dispose(); throw 'Another UI validation run is active.' }
try {
    Invoke-Probe 'stores' $runPath
    $protectedDir = Join-Path $runPath 'protected-directory'
    New-Item -ItemType Directory -Path $protectedDir | Out-Null
    Copy-Item -LiteralPath "$runPath/legacy/settings.ini" -Destination $protectedDir
    $originalAcl = Get-Acl -LiteralPath $protectedDir
    $protectedAcl = Get-Acl -LiteralPath $protectedDir
    $userSid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $denyCreate = [Security.AccessControl.FileSystemAccessRule]::new($userSid, 'CreateFiles, CreateDirectories', 'Deny')
    $protectedAcl.AddAccessRule($denyCreate)
    Set-Acl -LiteralPath $protectedDir -AclObject $protectedAcl
    try {
        Invoke-Probe 'protected-directory' $protectedDir
    } finally {
        Set-Acl -LiteralPath $protectedDir -AclObject $originalAcl
    }
    Invoke-Probe 'ui' $runPath
    $faultPath = Join-Path $runPath 'abrupt-exit'
    New-Item -ItemType Directory -Path $faultPath | Out-Null
    $ready = Join-Path $faultPath 'checkpoint-ready'
    $owned = Start-Process -FilePath $exe -ArgumentList @('crash-write', ('"' + $media + '"'), ('"' + $faultPath + '"')) `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput "$runPath/crash-write.log" -RedirectStandardError "$runPath/crash-write-errors.log"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    try {
        while (!(Test-Path -LiteralPath $ready) -and !$owned.HasExited -and $timer.Elapsed.TotalSeconds -lt 15) {
            Start-Sleep -Milliseconds 50
        }
        if (!(Test-Path -LiteralPath $ready) -or $owned.HasExited) { throw 'Settings process did not reach its checkpoint.' }
    } finally {
        if (!$owned.HasExited) {
            $owned.Kill()
            if (!$owned.WaitForExit(5000)) { throw 'Settings checkpoint process did not terminate.' }
        }
    }
    @{ forcedTermination = $true; processId = $owned.Id; exitCode = $owned.ExitCode; elapsedSeconds = $timer.Elapsed.TotalSeconds } |
        ConvertTo-Json | Set-Content -LiteralPath "$faultPath/termination.json" -Encoding utf8
    Invoke-Probe 'crash-read' $faultPath
} finally {
    $validationLock.ReleaseMutex()
    $validationLock.Dispose()
}
