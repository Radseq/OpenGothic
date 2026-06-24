@echo off
setlocal

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul

set "SQLITE_VERSION=3530200"
set "SQLITE_YEAR=2026"
set "DOWNLOADS=downloads\sqlite"
set "DLL_ZIP=%DOWNLOADS%\sqlite-dll-win-x64-%SQLITE_VERSION%.zip"
set "AMALG_ZIP=%DOWNLOADS%\sqlite-amalgamation-%SQLITE_VERSION%.zip"
set "DLL_DIR=%DOWNLOADS%\dll"
set "AMALG_DIR=%DOWNLOADS%\amalgamation"

set "SQLITE_BIN=thirdparty\sqlite\bin"
set "SQLITE_INCLUDE=thirdparty\sqlite\include"
set "SQLITE_LIB=thirdparty\sqlite\lib"

echo [sqlite] Preparing directories...
if not exist "%DOWNLOADS%" mkdir "%DOWNLOADS%"
if not exist "%DLL_DIR%" mkdir "%DLL_DIR%"
if not exist "%AMALG_DIR%" mkdir "%AMALG_DIR%"
if not exist "%SQLITE_BIN%" mkdir "%SQLITE_BIN%"
if not exist "%SQLITE_INCLUDE%" mkdir "%SQLITE_INCLUDE%"
if not exist "%SQLITE_LIB%" mkdir "%SQLITE_LIB%"

echo [sqlite] Downloading official SQLite Windows x64 DLL...
curl -L --fail -o "%DLL_ZIP%" "https://www.sqlite.org/%SQLITE_YEAR%/sqlite-dll-win-x64-%SQLITE_VERSION%.zip"
if errorlevel 1 goto :download_failed

echo [sqlite] Downloading official SQLite amalgamation headers...
curl -L --fail -o "%AMALG_ZIP%" "https://www.sqlite.org/%SQLITE_YEAR%/sqlite-amalgamation-%SQLITE_VERSION%.zip"
if errorlevel 1 goto :download_failed

echo [sqlite] Extracting packages...
tar -xf "%DLL_ZIP%" -C "%DLL_DIR%"
if errorlevel 1 goto :extract_failed
tar -xf "%AMALG_ZIP%" -C "%AMALG_DIR%"
if errorlevel 1 goto :extract_failed

echo [sqlite] Installing sqlite3.dll and sqlite3.def...
copy /Y "%DLL_DIR%\sqlite3.dll" "%SQLITE_BIN%\" >nul
copy /Y "%DLL_DIR%\sqlite3.def" "%SQLITE_BIN%\" >nul

echo [sqlite] Installing sqlite3.h...
for /R "%AMALG_DIR%" %%F in (sqlite3.h) do copy /Y "%%F" "%SQLITE_INCLUDE%\" >nul

if not exist "%SQLITE_INCLUDE%\sqlite3.h" goto :missing_header
if not exist "%SQLITE_BIN%\sqlite3.dll" goto :missing_dll
if not exist "%SQLITE_BIN%\sqlite3.def" goto :missing_def

where lib.exe >nul 2>nul
if errorlevel 1 goto :need_vs_prompt

echo [sqlite] Generating sqlite3.lib with Visual Studio lib.exe...
lib /def:"%SQLITE_BIN%\sqlite3.def" /machine:x64 /out:"%SQLITE_LIB%\sqlite3.lib"
if errorlevel 1 goto :lib_failed

echo.
echo [sqlite] Done.
echo [sqlite] Files installed:
echo   %SQLITE_INCLUDE%\sqlite3.h
echo   %SQLITE_LIB%\sqlite3.lib
echo   %SQLITE_BIN%\sqlite3.dll
echo.
echo [sqlite] Now rebuild:
echo   cmake --build build --config Debug --target Gothic2Notr
goto :ok

:need_vs_prompt
echo.
echo [sqlite] Download/extract done, but lib.exe was not found.
echo [sqlite] Open "x64 Native Tools Command Prompt for VS" and run:
echo   cd /d "%CD%"
echo   lib /def:%SQLITE_BIN%\sqlite3.def /machine:x64 /out:%SQLITE_LIB%\sqlite3.lib
echo   cmake --build build --config Debug --target Gothic2Notr
goto :ok

:download_failed
echo [sqlite] ERROR: download failed.
goto :fail

:extract_failed
echo [sqlite] ERROR: extract failed.
goto :fail

:missing_header
echo [sqlite] ERROR: sqlite3.h was not found after extract.
goto :fail

:missing_dll
echo [sqlite] ERROR: sqlite3.dll was not found after extract.
goto :fail

:missing_def
echo [sqlite] ERROR: sqlite3.def was not found after extract.
goto :fail

:lib_failed
echo [sqlite] ERROR: lib.exe failed to generate sqlite3.lib.
goto :fail

:fail
popd >nul
exit /b 1

:ok
popd >nul
exit /b 0
