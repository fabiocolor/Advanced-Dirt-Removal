# Build Windows installer with Inno Setup
# Run from repo root: powershell -ExecutionPolicy Bypass -File installers\win\build_installer.ps1

$iss = Join-Path $PSScriptRoot "AdvancedDirtRemoval.iss"
$iscc = Get-Command iscc -ErrorAction SilentlyContinue

if (-not $iscc) {
    Write-Host "Inno Setup (iscc) not found in PATH. Install from https://jrsoftware.org/isinfo.php" -ForegroundColor Red
    exit 1
}

& $iscc.Source $iss
