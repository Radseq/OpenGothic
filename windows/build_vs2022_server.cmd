@echo off
setlocal

cd /d "%~dp0.."

if not exist "build\mmo_cpp_server\OpenGothicMmoServerCpp.sln" (
  call windows\configure_vs2022_server.cmd
  if errorlevel 1 exit /b 1
)

cmake --build build\mmo_cpp_server --config RelWithDebInfo --target mmo_udp_server
exit /b %errorlevel%
