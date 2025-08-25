@echo off
setlocal ENABLEDELAYEDEXPANSION
echo Installing Advanced Dirt Removal (Windows)

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
set "SRC=%REPO_ROOT%\Fusion\Advanced Dirt Removal.setting"
set "DEST=%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\Fusion\Macros"

if not exist "%SRC%" (
  echo ERROR: Missing file: %SRC%
  pause
  exit /b 1
)

if not exist "%DEST%" mkdir "%DEST%"
copy /Y "%SRC%" "%DEST%" >nul
echo Installed to: %DEST%
echo Restart Resolve/Fusion to load the tool.
pause

