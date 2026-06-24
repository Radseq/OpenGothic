@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0.."
set "GAME_PATH=%~1"
if "%GAME_PATH%"=="" set "GAME_PATH=C:\Program Files (x86)\Steam\steamapps\common\Gothic II"

pushd "%ROOT%" >nul

if not exist "build\opengothic\Debug\Gothic2Notr.exe" (
  echo [runtime-sqlite] ERROR: build\opengothic\Debug\Gothic2Notr.exe not found.
  echo [runtime-sqlite] Run:
  echo   cmake --build build --config Debug --target Gothic2Notr
  popd >nul
  exit /b 1
)

if not exist "!GAME_PATH!" (
  echo [runtime-sqlite] ERROR: Gothic II path not found:
  echo   !GAME_PATH!
  echo.
  echo [runtime-sqlite] Usage:
  echo   tools\run_runtime_sqlite_g2notr.cmd "C:\path\to\Gothic II"
  popd >nul
  exit /b 1
)

if not exist "runtime" mkdir runtime

echo [runtime-sqlite] Game path:
echo   !GAME_PATH!
echo [runtime-sqlite] Runtime DB:
echo   runtime\g2notr.sqlite
echo.

build\opengothic\Debug\Gothic2Notr.exe -g "!GAME_PATH!" -g2 -nomenu -mmo-sqlite runtime\g2notr.sqlite -mmo-sqlite-interval-ms 5000

popd >nul
exit /b %ERRORLEVEL%
