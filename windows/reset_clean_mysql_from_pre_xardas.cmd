@echo off
setlocal

cd /d "%~dp0.."

call "%~dp0mmo_env.cmd"

set "SQLITE_DB=runtime\g2notr_ch1_pre_xardas.sqlite"

if not exist "%SQLITE_DB%" (
  echo Missing "%SQLITE_DB%".
  echo Run windows\capture_pre_xardas_sqlite.cmd first, then run this reset again.
  exit /b 1
)

where py >nul 2>nul
if errorlevel 1 goto use_python

py -3 tools\run_mmo_step55_clean_mysql_from_pre_xardas.py ^
  --sqlite "%SQLITE_DB%" ^
  --mysql-url "%MYSQL_URL%" ^
  --i-understand-this-drops-database
exit /b %errorlevel%

:use_python
python tools\run_mmo_step55_clean_mysql_from_pre_xardas.py ^
  --sqlite "%SQLITE_DB%" ^
  --mysql-url "%MYSQL_URL%" ^
  --i-understand-this-drops-database
exit /b %errorlevel%
