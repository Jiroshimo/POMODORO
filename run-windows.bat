@echo off
setlocal
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "PATH=%MSYS2_ROOT%\ucrt64\bin;%PATH%"
where cmake >nul 2>&1
if errorlevel 1 (
  echo Install the MSYS2 UCRT64 dependencies listed in README.md first.
  pause
  exit /b 1
)
cmake -S "%~dp0." -B "%~dp0build-windows" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
if errorlevel 1 goto :failed
cmake --build "%~dp0build-windows" --parallel
if errorlevel 1 goto :failed
start "" "%~dp0build-windows\stash.exe"
exit /b 0
:failed
echo Build failed. See README.md for setup instructions.
pause
exit /b 1
