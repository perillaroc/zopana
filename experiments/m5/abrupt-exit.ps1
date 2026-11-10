param([Parameter(Mandatory)][string]$BuildDir, [Parameter(Mandatory)][string]$FixtureDir)
$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$fixturePath = (Resolve-Path -LiteralPath $FixtureDir).Path
$faultPath = Join-Path $buildPath 'abrupt-exit'
New-Item -ItemType Directory -Path $faultPath -Force | Out-Null
$ready = Join-Path $faultPath 'checkpoint-ready'
if (Test-Path -LiteralPath $ready) { Remove-Item -LiteralPath $ready }
$executable = Join-Path $buildPath 'zopana_m5.exe'
$owned = Start-Process -FilePath $executable -ArgumentList @('crash-write', ('"' + $fixturePath + '"'), ('"' + $faultPath + '"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$buildPath/abrupt-write.log" -RedirectStandardError "$buildPath/abrupt-write-errors.log"
$timer = [Diagnostics.Stopwatch]::StartNew()
try {
    while (!(Test-Path -LiteralPath $ready) -and !$owned.HasExited -and $timer.Elapsed.TotalSeconds -lt 20) { Start-Sleep -Milliseconds 100 }
    if (!(Test-Path -LiteralPath $ready)) { throw 'Checkpoint child did not finish its periodic atomic write.' }
} finally {
    if (!$owned.HasExited) {
        $current = Get-Process -Id $owned.Id
        if ($current.Path -ne $executable -or $current.StartTime -ne $owned.StartTime) { throw 'Owned checkpoint child identity changed; refusing to terminate.' }
        Stop-Process -Id $owned.Id -Force
        $owned.WaitForExit()
    }
}
& $executable 'crash-read' $fixturePath $faultPath 2>&1 | Out-File -Encoding utf8 "$buildPath/abrupt-read.log"
if ($LASTEXITCODE) { throw 'Checkpoint did not survive abrupt termination.' }
@{ forcedTermination = $true; checkpointWasDurable = $true; processId = $owned.Id; exitCode = $owned.ExitCode; elapsedSeconds = $timer.Elapsed.TotalSeconds } | ConvertTo-Json | Set-Content -Encoding utf8 "$faultPath/termination.json"
Write-Output 'PASS abrupt exit and checkpoint recovery'
