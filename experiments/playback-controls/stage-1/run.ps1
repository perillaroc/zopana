param(
    [Parameter(Mandatory)] [string]$BuildDir,
    [Parameter(Mandatory)] [string]$FixtureDir,
    [Parameter(Mandatory)] [string]$QtRoot,
    [ValidateScript({ $_ -gt 0 })] [double]$NativeDpr = 1,
    [ValidateSet('Preferences', 'Settings', 'Seek')]
    [string[]]$Checks = @('Preferences', 'Settings', 'Seek')
)

$ErrorActionPreference = 'Stop'
$probeArgs = @{
    BuildDir = $BuildDir
    FixtureDir = $FixtureDir
    QtRoot = $QtRoot
    NativeDpr = $NativeDpr
}
$savedEnvironment = @{}
foreach ($name in 'PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_SCALE_FACTOR_ROUNDING_POLICY', 'QT_SCALE_FACTOR') {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name)
}

try {
    foreach ($check in $Checks) {
        Write-Output "Running $check"
        $script = Join-Path $PSScriptRoot ('run-' + $check.ToLowerInvariant() + '.ps1')
        & $script @probeArgs
    }
} finally {
    foreach ($name in $savedEnvironment.Keys) {
        Set-Item -LiteralPath "Env:$name" -Value $savedEnvironment[$name]
    }
}
