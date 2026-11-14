#requires -version 5.1
[CmdletBinding(DefaultParameterSetName='Independent')]
param(
    [Parameter(Mandatory)][string]$Archive,
    [Parameter(Mandatory)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$ExpectedSha256,
    [string]$EvidenceRoot = (Join-Path $env:TEMP 'zopana-m6-checks'),
    [Parameter(ParameterSetName='Independent')]
    [Parameter(ParameterSetName='Startup')][string]$Fixture,
    [Parameter(ParameterSetName='Startup')][switch]$StartupOnly,
    [Parameter(ParameterSetName='Inspect')][switch]$InspectOnly
)

$ErrorActionPreference = 'Stop'
$folder = Join-Path $EvidenceRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6))
$null = New-Item -ItemType Directory -Path $folder -Force
$record = [ordered]@{ mode = $PSCmdlet.ParameterSetName; started = (Get-Date).ToString('o'); status = 'FAILED'; checks = [ordered]@{} }
$player = $null
$sample = $null
$originalPath = $env:PATH
$overrides = @(Get-ChildItem Env: | Where-Object { $_.Name -like 'QT_*' -or $_.Name -eq 'CMAKE_PREFIX_PATH' })

function Confirm-Step([string]$Key, [string]$Prompt) {
    $record.checks[$Key] = (Read-Host "$Prompt [Y/N]") -eq 'Y'
    if (!$record.checks[$Key]) { throw "Operator did not confirm $Key." }
}

function Test-MetadataWorker([string]$FixturePath) {
    $info = [Diagnostics.ProcessStartInfo]::new($exe, '--playlist-metadata-worker')
    $info.WorkingDirectory = $appDir
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $utf8 = [Text.UTF8Encoding]::new($false)
    $info.StandardOutputEncoding = $utf8
    $info.StandardErrorEncoding = $utf8
    $requests = @(
        (@{ path = $FixturePath; media = $true } | ConvertTo-Json -Compress),
        (@{ path = (Join-Path $folder 'missing.mp4'); media = $true } | ConvertTo-Json -Compress)
    )
    $worker = [Diagnostics.Process]::Start($info)
    try {
        $stdout = $worker.StandardOutput.ReadToEndAsync()
        $stderr = $worker.StandardError.ReadToEndAsync()
        $payload = $utf8.GetBytes(($requests -join "`n") + "`n")
        $worker.StandardInput.BaseStream.Write($payload, 0, $payload.Length)
        $worker.StandardInput.Close()
        if (!$worker.WaitForExit(15000)) { throw 'The packaged metadata worker did not exit on EOF.' }
        $stdout.Result | Set-Content -LiteralPath (Join-Path $folder 'metadata-worker.jsonl') -Encoding UTF8
        $stderr.Result | Set-Content -LiteralPath (Join-Path $folder 'metadata-worker.log') -Encoding UTF8
        $responses = @($stdout.Result.Trim() -split '\r?\n' | ForEach-Object { $_ | ConvertFrom-Json })
        if ($worker.ExitCode -ne 0 -or $responses.Count -ne 2 -or
            $responses[0].media.read -ne $true -or $responses[0].media.duration -le 0 -or
            $responses[0].media.width -le 0 -or $responses[0].media.height -le 0 -or
            $responses[1].media.read -ne $false) {
            throw 'The packaged metadata worker did not return the expected JSON responses.'
        }
        $record.checks.metadataWorker = $true
        $record.workerMedia = $responses[0].media
    } finally {
        if (!$worker.HasExited) { $worker.Kill(); $null = $worker.WaitForExit(5000) }
        $worker.Dispose()
    }
}

try {
    $record.archive = (Resolve-Path -LiteralPath $Archive).Path
    $record.sha256 = (Get-FileHash -LiteralPath $record.archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($record.sha256 -ne $ExpectedSha256) { throw 'Release ZIP SHA-256 mismatch.' }
    $record.os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, OSArchitecture
    $record.machine = Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer, Model, HypervisorPresent
    $record.cpuArchitecture = @(Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Architecture -Unique)
    $record.display = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, CurrentHorizontalResolution, CurrentVerticalResolution)
    if ($record.cpuArchitecture.Count -ne 1 -or $record.cpuArchitecture[0] -ne 9 -or ![Environment]::Is64BitProcess) {
        throw 'Use 64-bit PowerShell on x64 Windows.'
    }
    $record.sdkTools = @(Get-Command qmake,qmake6,qtpaths6,windeployqt -CommandType Application -ErrorAction SilentlyContinue | Select-Object Name, Source)
    $record.sdkVariableNames = @($overrides | Select-Object -ExpandProperty Name) +
        @(Get-ChildItem Env: | Where-Object { $_.Name -in 'QTDIR','Qt6_DIR','MPV_ROOT' } | Select-Object -ExpandProperty Name)
    $record.developmentPathEntries = @($originalPath -split ';' |
        Where-Object { $_ -match '[\\/](qt|libmpv|Microsoft Visual Studio|Windows Kits)([\\/]|$)' })
    # Use an actual non-ASCII path on Windows PowerShell 5.1 and on PowerShell 7.
    $extract = Join-Path $folder ("$([char]0x53D1)$([char]0x5E03)$([char]0x5305) Windows x64")
    Expand-Archive -LiteralPath $record.archive -DestinationPath $extract
    $appDir = Join-Path $extract 'zopana-windows-x64'
    $exe = Join-Path $appDir 'zopana.exe'
    $requiredFiles = 'zopana.exe','README.txt','qt.conf','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6OpenGL.dll','Qt6OpenGLWidgets.dll','Qt6Svg.dll','platforms/qwindows.dll','libmpv-2.dll',
        'notices/lucide-LICENSE.txt','notices/SOURCES.txt','notices/libmpv-LICENSE.LGPL','notices/LGPL-3.0-only.txt','notices/GPL-3.0-only.txt',
        'notices/qtbase-6.11.3.spdx.json','notices/qtsvg-6.11.3.spdx.json','notices/manifest.json'
    foreach ($file in $requiredFiles) {
        if (!(Test-Path -LiteralPath (Join-Path $appDir $file) -PathType Leaf)) { throw "Missing packaged file $file." }
    }
    if ((Get-Item -LiteralPath $exe).Name -cne 'zopana.exe') { throw 'Expected lowercase zopana.exe.' }
    $manifest = Get-Content -LiteralPath (Join-Path $appDir 'notices/manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $files = @{}
    foreach ($file in (Get-ChildItem -LiteralPath $appDir -Recurse -File -Force)) {
        $relative = $file.FullName.Substring($appDir.Length + 1).Replace('\', '/')
        if ($relative -ne 'notices/manifest.json') { $files.Add($relative, $file) }
    }
    foreach ($entry in $manifest.files) {
        $file = $files[$entry.path]
        if (!$file -or $file.Length -ne $entry.bytes -or
            (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "Packaged file does not match the manifest: $($entry.path)"
        }
        $files.Remove($entry.path)
    }
    if ($files.Count) { throw "Unlisted packaged files: $($files.Keys -join ', ')." }
    $record.checks.manifest = $true
    $record.manifestFiles = @($manifest.files).Count
    $record.executable = $exe
    $record.exeSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($InspectOnly) { $record.status = 'PREFLIGHT_ONLY'; return }

    if (!$StartupOnly) {
        if ($record.sdkTools.Count -or $record.sdkVariableNames.Count -or $record.developmentPathEntries.Count) {
            throw 'Development tools or environment overrides detected. Use the independent Windows system.'
        }
        Confirm-Step 'independentSystem' 'Confirm this Windows installation is independent and has no Qt SDK or libmpv development kit'
        if (!$Fixture) { $Fixture = Join-Path $PSScriptRoot 'sample-motion.mp4' }
    }
    if ($Fixture) {
        $fixturePath = (Resolve-Path -LiteralPath $Fixture).Path
        $sample = Join-Path $folder ("$([char]0x6709)$([char]0x58F0) sample" + [IO.Path]::GetExtension($fixturePath))
        Copy-Item -LiteralPath $fixturePath -Destination $sample
        $record.sampleSha256 = (Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($item in $overrides) { Remove-Item -LiteralPath "Env:$($item.Name)" }
    if ($sample) { Test-MetadataWorker $sample }
    $profile = Join-Path $folder 'test profile'
    $player = Start-Process -FilePath $exe -WorkingDirectory $appDir -PassThru -ArgumentList ('--validation-profile "' + $profile + '"') `
        -RedirectStandardOutput (Join-Path $folder 'stdout.log') -RedirectStandardError (Join-Path $folder 'player.log')
    $null = $player.Handle
    $startup = [Diagnostics.Stopwatch]::StartNew()
    do {
        Start-Sleep -Milliseconds 100
        $player.Refresh()
    } while (!$player.HasExited -and $player.MainWindowHandle -eq 0 -and $startup.ElapsedMilliseconds -lt 15000)
    if ($player.HasExited -or $player.MainWindowHandle -eq 0) { throw 'zopana.exe did not open a window.' }
    $record.checks.windowOpened = $true
    $record.windowStartupMs = $startup.ElapsedMilliseconds

    $modules = @(Get-Process -Id $player.Id -Module | Select-Object ModuleName,FileName)
    $modules | Export-Csv -LiteralPath (Join-Path $folder 'modules.csv') -Encoding UTF8 -NoTypeInformation
    $crtModules = 'msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','vcruntime140.dll','vcruntime140_1.dll'
    $expectedModules = @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6OpenGL.dll','Qt6OpenGLWidgets.dll','Qt6Svg.dll','qwindows.dll','libmpv-2.dll') + $crtModules
    $record.missingModules = @($expectedModules |
        Where-Object { $_ -notin $modules.ModuleName })
    $bundle = $appDir.TrimEnd('\') + '\'
    $record.externalAppModules = @($modules | Where-Object {
        ($_.ModuleName -like 'Qt6*.dll' -or $_.ModuleName -in $expectedModules -or $_.ModuleName -eq 'qmodernwindowsstyle.dll') -and
        !$_.FileName.StartsWith($bundle, [StringComparison]::OrdinalIgnoreCase)
    })
    if ($record.missingModules.Count -or $record.externalAppModules.Count) { throw 'Runtime module validation failed.' }

    if (!$StartupOnly) {
        Write-Host "Use this sample in zopana: $sample"
        Confirm-Step 'playPause' 'Open the sample with Ctrl+O. Did the picture move and sound play, and did Space pause it?'
        Confirm-Step 'zoomPanVolume' 'Did the paused picture zoom around the pointer and pan, and did switching to standard mode let the wheel change volume without changing the framing?'
        Confirm-Step 'fullscreenPlaylist' 'Did F11 enter and leave fullscreen, did the playlist detach, and did closing the detached playlist keep the player open?'
        Confirm-Step 'rotateAndSort' 'Did Alt+K rotate the picture and 2 resize the playback window? Add a second file with Ctrl+Shift+O (the same source sample at a different path works). Did Duration sort keep the current file playing?'
        Write-Host "Use this capture directory in Settings: $(Join-Path $profile 'captures')"
        Confirm-Step 'captureAndLicenses' 'Set the capture directory in Settings. Did Ctrl+Shift+S save a PNG there, and did Help > About > Third-party licenses show the full license?'
        Confirm-Step 'settings' 'Did F5 open settings and, after closing settings, did F1 open shortcut help? Close the shortcut help before continuing'
        Confirm-Step 'operatorExit' 'Please close the main window. Did the application and detached playlist both close?'
    } elseif (!$player.CloseMainWindow()) {
        throw 'Could not close the startup test window.'
    }
    if (!$player.WaitForExit(15000)) { throw 'zopana.exe did not exit within 15 seconds.' }
    $record.exitCode = $player.ExitCode
    if ($record.exitCode -ne 0) { throw "zopana.exe exited with code $($record.exitCode)." }
    if (!(Test-Path -LiteralPath (Join-Path $profile 'config/settings.ini'))) { throw 'No settings file was written to the isolated profile.' }
    $record.checks.isolatedProfile = $true
    $record.status = if ($StartupOnly) { 'HOST_STARTUP_ONLY' } else { 'INDEPENDENT_PASS' }
} catch {
    $record.error = $_.Exception.Message
    throw
} finally {
    if ($player) {
        if (!$player.HasExited -and (!$player.CloseMainWindow() -or !$player.WaitForExit(5000))) { $player.Kill() }
        $player.Dispose()
    }
    $env:PATH = $originalPath
    foreach ($item in $overrides) { Set-Item -LiteralPath "Env:$($item.Name)" -Value $item.Value }
    $record.finished = (Get-Date).ToString('o')
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $folder 'result.json') -Encoding UTF8
    Write-Host "Status: $($record.status). Evidence: $folder"
}
