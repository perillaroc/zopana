# Portable packages and release validation

## Create a Windows x64 portable package

Run the packager after a Release build in a Visual Studio x64 developer terminal. Use the same Qt kit and libmpv SDK. The command below reuses `$build`, `$qt`, and `$mpv` from the build and creates a fresh output folder each time. [Build the player](./build.md#build-player)

```powershell
$dist = Join-Path $PWD ('build\package-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
& .\packaging\package-windows.ps1 -BuildDir $build -QtRoot $qt -MpvRoot $mpv -OutputDir $dist
```

The script uses `windeployqt` for Qt and its platform plugins, copies libmpv from the SDK, and copies the VC++ runtime from the Visual Studio x64 redistributable directory. It produces `zopana-windows-x64.zip` and `zopana-windows-x64.zip.sha256`. The packaged `README.txt` covers launch and controls, and `LICENSE` contains the Apache 2.0 license for zopana's own source code. `notices/manifest.json` records each other file's byte count and SHA-256. [Verify, extract, and run the ZIP](../quick-start.md)

## Publish on GitHub

Branch pushes and pull requests trigger CI. The workflow builds and checks the portable package on Windows Server 2022. Download the ZIP and its SHA-256 from the `zopana-windows-x64` artifact on the Actions page.

Pushing a `vX.Y.Z` tag triggers the Release workflow. It reuses the same build and package checks, publishes the ZIP and SHA-256 with the repository's `GITHUB_TOKEN`, and generates release notes. After committing and pushing the workflows, create and push a tag on the commit you want to release.

```powershell
git tag -a v0.1.0 -m "v0.1.0"
git push origin v0.1.0
```

## Validate on a clean Windows system

Use a clean Windows 11 x64 installation or virtual machine with its default system PATH. Prepare the ZIP, its checksum file, and the validation tools listed in `experiments/m6/README.md`. The script checks the ZIP and file manifest, extracts the package into a folder with Chinese characters and spaces, verifies the paths from which Qt and libmpv load, and reads the test clip's duration and resolution using the same EXE's standalone worker mode. It uses a separate configuration directory for the GUI. The operator checks playback, input, the separate playlist window, screenshots, and settings.

Run the following commands in regular 64-bit PowerShell. Test the player and confirm each result in the terminal. `result.json` records the system, hashes, and results. `modules.csv` lists loaded DLLs and `metadata-worker.jsonl` records the metadata probe responses. On a development machine, `-StartupOnly -Fixture <clip-with-audio>` runs the automated startup checks and marks the result as a host-only check.

```powershell
$expected = ((Get-Content .\zopana-windows-x64.zip.sha256 -Raw).Trim() -split '\s+')[0]
& .\check-clean-windows.ps1 -Archive .\zopana-windows-x64.zip -ExpectedSha256 $expected
```

## Third-party dependencies and licenses

The player uses icons from Lucide. Open **Help → Third-party Licenses** (帮助 → 第三方许可) for the full ISC and Feather MIT texts. The package also includes them in `notices/lucide-LICENSE.txt`.

`notices/SOURCES.txt` identifies the dependency versions, source code, and build origins. The same directory contains the libmpv SDK license, the Qt LGPLv3 and GPLv3 texts, and the two Qt component SPDX inventories. Before publishing, verify the distribution materials for the third-party components.
