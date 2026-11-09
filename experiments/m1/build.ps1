param(
    [string]$BuildDir = "$PSScriptRoot/../../../../build/ui-m1-release-20261109",
    [string]$QtRoot = 'C:/lang/qt/6.11.3/msvc2022_64',
    [string]$VsRoot = 'C:/Program Files/Microsoft Visual Studio/2022/Community'
)
$ErrorActionPreference = 'Stop'
$buildPath = [IO.Path]::GetFullPath($BuildDir)
New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
& "$VsRoot/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:PATH = "$QtRoot/bin;C:/lang/qt/Tools/Ninja;$env:PATH"
$sdkPath = (Resolve-Path -LiteralPath "$PSScriptRoot/../../../../deps/libmpv/0.42.0-windows-x64").Path
cmake -S "$PSScriptRoot/../.." -B $buildPath -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release -DZOPANA_BUILD_M1_PROBE=ON "-DCMAKE_PREFIX_PATH=$QtRoot" "-DMPV_ROOT=$sdkPath" 2>&1 | Tee-Object -FilePath "$buildPath/configure.log"
if ($LASTEXITCODE) { throw 'M1 configure failed' }
cmake --build $buildPath --target zopana zopana_m1 zopana_m0_regression 2>&1 | Tee-Object -FilePath "$buildPath/build.log"
if ($LASTEXITCODE) { throw 'M1 build failed' }
Get-FileHash -Algorithm SHA256 -LiteralPath "$buildPath/zopana.exe", "$buildPath/zopana_m1.exe", "$buildPath/libmpv-2.dll" | Format-List | Out-File -Encoding utf8 "$buildPath/build-id.txt"
