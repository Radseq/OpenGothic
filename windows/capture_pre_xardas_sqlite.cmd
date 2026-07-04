@echo off
setlocal

cd /d "%~dp0.."

set "CONFIG=RelWithDebInfo"
set "CLIENT_EXE=build\opengothic\%CONFIG%\Gothic2Notr.exe"
set "GOTHIC2_DIR=C:\Program Files (x86)\Steam\steamapps\common\Gothic II"
set "SQLITE_DB=runtime\g2notr_ch1_pre_xardas.sqlite"

if not exist "%CLIENT_EXE%" (
  echo Missing "%CLIENT_EXE%".
  echo Run windows\build_vs2022_client.cmd first.
  exit /b 1
)

if not exist "%GOTHIC2_DIR%" (
  echo Missing Gothic II directory: "%GOTHIC2_DIR%".
  echo Edit GOTHIC2_DIR in this script if your game is installed elsewhere.
  exit /b 1
)

if not exist "runtime" mkdir "runtime"

"%CLIENT_EXE%" ^
  -g "%GOTHIC2_DIR%" ^
  -g2 ^
  -mmo-sqlite "%SQLITE_DB%" ^
  -mmo-sqlite-capture-pre-start-exit
exit /b %errorlevel%
