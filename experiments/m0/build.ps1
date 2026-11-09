param(
    [string]$BuildDir = "$PSScriptRoot/../../../../build/ui-m0-20261109",
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [string]$VsRoot = 'C:/Program Files/Microsoft Visual Studio/2022/Community'
)
$ErrorActionPreference = 'Stop'
$buildPath = [IO.Path]::GetFullPath($BuildDir)
New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
& "$VsRoot/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC x64 environment unavailable' }
$env:PATH = "$QtRoot/bin;C:/lang/qt/Tools/Ninja;$env:PATH"
$sdkPath = (Resolve-Path -LiteralPath "$PSScriptRoot/../../../../deps/libmpv/0.42.0-windows-x64").Path
& cl.exe 2>&1 | Out-File -Encoding utf8 "$buildPath/compiler.log"
cmake -S $PSScriptRoot -B $buildPath -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtRoot" "-DMPV_ROOT=$sdkPath" 2>&1 | Tee-Object -FilePath "$buildPath/configure.log"
if ($LASTEXITCODE) { throw 'M0 configure failed' }
cmake --build $buildPath --target zopana zopana_m0 2>&1 | Tee-Object -FilePath "$buildPath/build.log"
if ($LASTEXITCODE) { throw 'M0 build failed' }
Get-FileHash -Algorithm SHA256 -LiteralPath "$buildPath/zopana_m0.exe", "$buildPath/zopana/zopana.exe", "$buildPath/libmpv-2.dll" | Format-List | Out-File -Encoding utf8 "$buildPath/build-id.txt"
