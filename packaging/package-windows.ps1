[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDir,
    [Parameter(Mandatory)][string]$QtRoot,
    [Parameter(Mandatory)][string]$MpvRoot,
    [Parameter(Mandatory)][string]$OutputDir
)

$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$qt = (Resolve-Path -LiteralPath $QtRoot).Path
$mpv = (Resolve-Path -LiteralPath $MpvRoot).Path
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
$name = 'zopana-windows-x64'
$stage = Join-Path $output $name
$zip = Join-Path $output "$name.zip"
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $zip)) {
    throw "Choose a fresh output directory. $stage or $zip already exists."
}
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
if ($cache -notcontains 'CMAKE_BUILD_TYPE:STRING=Release') { throw 'A Release build is required.' }
if (!$env:VCToolsRedistDir) { throw 'Run this script from the VS 2022 x64 Developer PowerShell.' }
$crt = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
$dllHash = (Get-FileHash (Join-Path $mpv 'libmpv-2.dll')).Hash
if ((Get-FileHash (Join-Path $build 'libmpv-2.dll')).Hash -ne $dllHash) {
    throw 'The build and the supplied libmpv SDK use different DLLs.'
}
if ((Get-Item (Join-Path $qt 'bin/Qt6Core.dll')).VersionInfo.FileVersion -ne '6.11.3.0' -or
    $dllHash -ne '94fb87247732c59915cd59808bc582e843a71c22214b7b6c1e5563aa8149cef4') {
    throw 'This package requires Qt 6.11.3 and the mpv 0.42.0 x64 LGPL release.'
}

New-Item -ItemType Directory -Path $stage -Force | Out-Null
$exe = Join-Path $stage 'zopana.exe'
Copy-Item -LiteralPath (Join-Path $build 'zopana.exe') -Destination $exe
Copy-Item -LiteralPath (Join-Path $mpv 'libmpv-2.dll') -Destination $stage
$previousPath = $env:PATH
try {
    $env:PATH = "$(Join-Path $qt 'bin');$previousPath"
    & (Join-Path $qt 'bin/windeployqt.exe') --release --no-compiler-runtime --no-translations `
        --no-system-d3d-compiler --no-system-dxc-compiler `
        --skip-plugin-types imageformats,iconengines,networkinformation,tls,generic,platforminputcontexts `
        --dir $stage $exe
    if ($LASTEXITCODE) { throw "windeployqt failed ($LASTEXITCODE)." }
} finally {
    $env:PATH = $previousPath
}
Copy-Item -Path (Join-Path $crt '*.dll') -Destination $stage
@('[Paths]', 'Prefix=.', 'Plugins=.') | Set-Content -LiteralPath (Join-Path $stage 'qt.conf') -Encoding ASCII
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.txt') -Destination $stage
New-Item -ItemType Directory -Path (Join-Path $stage 'notices') -Force | Out-Null
$notices = Join-Path $stage 'notices'
foreach ($file in 'SOURCES.txt','lucide-LICENSE.txt','LGPL-3.0-only.txt','GPL-3.0-only.txt') {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "notices/$file") -Destination $notices
}
Copy-Item -LiteralPath (Join-Path $mpv 'LICENSE.LGPL') -Destination (Join-Path $notices 'libmpv-LICENSE.LGPL')
foreach ($module in 'qtbase','qtsvg') {
    Copy-Item -LiteralPath (Join-Path $qt "sbom/$module-6.11.3.spdx.json") -Destination $notices
}
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
[ordered]@{ qt = '6.11.3'; libmpv = '0.42.0'; files = $files } | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $notices 'manifest.json') -Encoding UTF8

# Include the application folder so extraction never mixes its DLLs into other applications.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$partial = Join-Path $output ".$name-$([guid]::NewGuid()).zip"
try {
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $partial, [IO.Compression.CompressionLevel]::Optimal, $true)
    Move-Item -LiteralPath $partial -Destination $zip
} finally {
    if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial }
}
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name.zip" | Set-Content -LiteralPath "$zip.sha256" -Encoding ASCII
Write-Output $zip
