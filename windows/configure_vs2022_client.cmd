@echo off
setlocal

cd /d "%~dp0.."

if not exist "build" mkdir "build"
cmake -S . -B build -G "Visual Studio 17 2022"
exit /b %errorlevel%
