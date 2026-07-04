@echo off
setlocal

cd /d "%~dp0.."

call "%~dp0mmo_env.cmd"

set "CONFIG=RelWithDebInfo"
set "SERVER_EXE=build\mmo_cpp_server\%CONFIG%\mmo_udp_server.exe"
set "SESSION_KEY=local-dev-PC_HERO_TEST"
set "CHARACTER_KEY=PC_HERO"

if not exist "%SERVER_EXE%" (
  echo Missing "%SERVER_EXE%".
  echo Run windows\build_vs2022_server.cmd first.
  exit /b 1
)

"%SERVER_EXE%" ^
  --bind 127.0.0.1:29777 ^
  --mysql-url "%MYSQL_URL%"
rem Defaults are supplied by the server:
rem   --session-key "%SESSION_KEY%"
rem   --character-key "%CHARACTER_KEY%"
exit /b %errorlevel%
