@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install_windows.ps1" -InstallDependencies -Launch
if errorlevel 1 (
  echo Installation failed. See the error above.
  pause
  exit /b 1
)
