@echo off
setlocal

cd /d "%~dp0.."

set "CONFIG=RelWithDebInfo"
set "CLIENT_EXE=build\opengothic\%CONFIG%\Gothic2Notr.exe"
set "GOTHIC2_DIR=C:\Program Files (x86)\Steam\steamapps\common\Gothic II"
set "SESSION_KEY=local-dev-PC_HERO_TEST"

if not exist "%CLIENT_EXE%" (
  echo Missing "%CLIENT_EXE%".
  echo Run windows\build_vs2022_client.cmd first.
  exit /b 1
)

"%CLIENT_EXE%" ^
  -g "%GOTHIC2_DIR%" ^
  -g2 ^
  -mmo-client-server 127.0.0.1:29777 ^
  -mmo-action-session-key "%SESSION_KEY%"
exit /b %errorlevel%
