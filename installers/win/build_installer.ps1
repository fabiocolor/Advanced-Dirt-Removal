# Build the Windows installer: CMake Release build of the OFX plugin, then Inno Setup.
# Run from the repo root:
#   powershell -ExecutionPolicy Bypass -File installers\win\build_installer.ps1 [-Version 2.0.0] [-NoCuda]
# Needs Visual Studio 2022 (C++), CMake 3.21+, Inno Setup 6 (iscc), and the CUDA
# toolkit unless -NoCuda is given.
param(
    [string]$Version = "2.0.0",
    [switch]$NoCuda
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$build = Join-Path $root "build\release-win"

$iscc = Get-Command iscc -ErrorAction SilentlyContinue
if (-not $iscc) {
    $default = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    if (Test-Path $default) { $iscc = Get-Item $default } else {
        Write-Host "Inno Setup (iscc) not found. Install from https://jrsoftware.org/isinfo.php" -ForegroundColor Red
        exit 1
    }
}

$cuda = if ($NoCuda) { "OFF" } else { "ON" }
cmake -S $root -B $build -G "Visual Studio 17 2022" -A x64 -DADR_WITH_CUDA=$cuda -DADR_WITH_OPENCL=ON
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --config Release --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$bundle = Join-Path $build "AdvancedDirtRemoval.ofx.bundle"
if (-not (Test-Path (Join-Path $bundle "Contents\Win64\AdvancedDirtRemoval.ofx"))) {
    Write-Host "Plugin not found in $bundle" -ForegroundColor Red
    exit 1
}
$path = if ($iscc.Source) { $iscc.Source } else { $iscc.FullName }
& $path "/DMyAppVersion=$Version" "/DBundleDir=$bundle" (Join-Path $PSScriptRoot "AdvancedDirtRemoval.iss")
exit $LASTEXITCODE
