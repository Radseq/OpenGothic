@echo off
setlocal

cd /d "%~dp0.."

call windows\build_vs2022_client.cmd
if errorlevel 1 exit /b 1

call windows\build_vs2022_server.cmd
exit /b %errorlevel%
