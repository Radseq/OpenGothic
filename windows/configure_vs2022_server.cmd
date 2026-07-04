@echo off
setlocal

cd /d "%~dp0.."

if not exist "build\mmo_cpp_server" mkdir "build\mmo_cpp_server"
cmake -S server\cpp -B build\mmo_cpp_server -G "Visual Studio 17 2022"
exit /b %errorlevel%
