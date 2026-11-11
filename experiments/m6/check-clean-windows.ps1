#requires -version 5.1
[CmdletBinding(DefaultParameterSetName='Independent')]
param(
    [Parameter(Mandatory)][string]$Archive,
    [Parameter(Mandatory)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$ExpectedSha256,
    [string]$EvidenceRoot = (Join-Path $env:TEMP 'zopana-m6-checks'),
    [Parameter(ParameterSetName='Independent')][string]$Fixture,
    [Parameter(ParameterSetName='Startup')][switch]$StartupOnly,
    [Parameter(ParameterSetName='Inspect')][switch]$InspectOnly
)

$ErrorActionPreference = 'Stop'
$folder = Join-Path $EvidenceRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6))
$null = New-Item -ItemType Directory -Path $folder -Force
$record = [ordered]@{ mode = $PSCmdlet.ParameterSetName; started = (Get-Date).ToString('o'); status = 'FAILED'; checks = [ordered]@{} }
$player = $null
$originalPath = $env:PATH
$overrides = @(Get-ChildItem Env: | Where-Object { $_.Name -like 'QT_*' -or $_.Name -eq 'CMAKE_PREFIX_PATH' })

function Confirm-Step([string]$Key, [string]$Prompt) {
    $record.checks[$Key] = (Read-Host "$Prompt [Y/N]") -eq 'Y'
    if (!$record.checks[$Key]) { throw "Operator did not confirm $Key." }
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
    $requiredFiles = 'zopana.exe','README.txt','qt.conf','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6OpenGL.dll','Qt6OpenGLWidgets.dll','Qt6Svg.dll','platforms/qwindows.dll','libmpv-2.dll','notices/lucide-LICENSE.txt'
    foreach ($file in $requiredFiles) {
        if (!(Test-Path -LiteralPath (Join-Path $appDir $file) -PathType Leaf)) { throw "Missing packaged file $file." }
    }
    if ((Get-Item -LiteralPath $exe).Name -cne 'zopana.exe') { throw 'Expected lowercase zopana.exe.' }
    $record.executable = $exe
    $record.exeSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($InspectOnly) { $record.status = 'PREFLIGHT_ONLY'; return }

    if (!$StartupOnly) {
        if ($record.sdkTools.Count -or $record.sdkVariableNames.Count -or $record.developmentPathEntries.Count) {
            throw 'Development tools or environment overrides detected. Use the independent Windows system.'
        }
        Confirm-Step 'independentSystem' 'Confirm this Windows installation is independent and has no Qt SDK or libmpv development kit'
        if (!$Fixture) { $Fixture = Join-Path $PSScriptRoot 'sample-motion.mp4' }
        $fixturePath = (Resolve-Path -LiteralPath $Fixture).Path
        $sample = Join-Path $folder ("$([char]0x6709)$([char]0x58F0) sample" + [IO.Path]::GetExtension($fixturePath))
        Copy-Item -LiteralPath $fixturePath -Destination $sample
        $record.sampleSha256 = (Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($item in $overrides) { Remove-Item -LiteralPath "Env:$($item.Name)" }
    $player = Start-Process -FilePath $exe -WorkingDirectory $appDir -PassThru -WindowStyle Normal `
        -RedirectStandardOutput (Join-Path $folder 'stdout.log') -RedirectStandardError (Join-Path $folder 'player.log')
    $null = $player.Handle
    $startup = [Diagnostics.Stopwatch]::StartNew()
    do {
        Start-Sleep -Milliseconds 100
        $player.Refresh()
    } while (!$player.HasExited -and $player.MainWindowHandle -eq 0 -and $startup.ElapsedMilliseconds -lt 15000)
    if ($player.HasExited -or $player.MainWindowHandle -eq 0) { throw 'zopana.exe did not open a window.' }
    $record.checks.windowOpened = $true

    $modules = @(Get-Process -Id $player.Id -Module | Select-Object ModuleName,FileName)
    $modules | Export-Csv -LiteralPath (Join-Path $folder 'modules.csv') -Encoding UTF8 -NoTypeInformation
    $record.missingModules = @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6OpenGL.dll','Qt6OpenGLWidgets.dll','qwindows.dll','libmpv-2.dll' |
        Where-Object { $_ -notin $modules.ModuleName })
    $bundle = $appDir.TrimEnd('\') + '\'
    $record.externalAppModules = @($modules | Where-Object {
        ($_.ModuleName -like 'Qt6*.dll' -or $_.ModuleName -in 'qwindows.dll','qmodernwindowsstyle.dll','libmpv-2.dll') -and
        !$_.FileName.StartsWith($bundle, [StringComparison]::OrdinalIgnoreCase)
    })
    if ($record.missingModules.Count -or $record.externalAppModules.Count) { throw 'Runtime module validation failed.' }

    if (!$StartupOnly) {
        Write-Host "Use this sample in zopana: $sample"
        Confirm-Step 'playPause' 'Open the sample with Ctrl+O. Did the picture move and sound play, and did Space pause it?'
        Confirm-Step 'zoomPanVolume' 'Did the paused picture zoom around the pointer and pan, and did switching to standard mode let the wheel change volume without changing the framing?'
        Confirm-Step 'fullscreenPlaylist' 'Did F11 enter and leave fullscreen, did the playlist detach, and did closing the detached playlist keep the player open?'
        Confirm-Step 'captureAndLicenses' 'Did Ctrl+Shift+S save a PNG, and did Help > About > Third-party licenses show the full license? Close the license dialog before continuing'
        Confirm-Step 'settings' 'Did F5 open settings and, after closing settings, did F1 open shortcut help? Close the shortcut help before continuing'
        Confirm-Step 'operatorExit' 'Please close the main window. Did the application and detached playlist both close?'
    } elseif (!$player.CloseMainWindow()) {
        throw 'Could not close the startup test window.'
    }
    if (!$player.WaitForExit(15000)) { throw 'zopana.exe did not exit within 15 seconds.' }
    $record.exitCode = $player.ExitCode
    if ($record.exitCode -ne 0) { throw "zopana.exe exited with code $($record.exitCode)." }
    $record.status = if ($StartupOnly) { 'HOST_STARTUP_ONLY' } else { 'INDEPENDENT_PASS' }
} catch {
    $record.error = $_.Exception.Message
    throw
} finally {
    if ($player -and !$player.HasExited) { $null = $player.CloseMainWindow() }
    $env:PATH = $originalPath
    foreach ($item in $overrides) { Set-Item -LiteralPath "Env:$($item.Name)" -Value $item.Value }
    $record.finished = (Get-Date).ToString('o')
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $folder 'result.json') -Encoding UTF8
    Write-Host "Status: $($record.status). Evidence: $folder"
}
