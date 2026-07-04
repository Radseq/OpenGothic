@echo off
setlocal

cd /d "%~dp0.."

if not exist "build\Gothic2Notr.sln" (
  call windows\configure_vs2022_client.cmd
  if errorlevel 1 exit /b 1
)

cmake --build build --config RelWithDebInfo --target Gothic2Notr
exit /b %errorlevel%
