@echo off
rem Shared Windows MMO dev defaults.
rem Windows connects to the Linux desktop MySQL server over ZeroTier.

if not defined GOTHIC_MMO_MYSQL_HOST set "GOTHIC_MMO_MYSQL_HOST=192.168.195.94"
if not defined GOTHIC_MMO_MYSQL_PORT set "GOTHIC_MMO_MYSQL_PORT=3306"
if not defined GOTHIC_MMO_MYSQL_USER set "GOTHIC_MMO_MYSQL_USER=gothic"
if not defined GOTHIC_MMO_MYSQL_PASSWORD set "GOTHIC_MMO_MYSQL_PASSWORD=gothic_dev_password"
if not defined GOTHIC_MMO_MYSQL_DATABASE set "GOTHIC_MMO_MYSQL_DATABASE=gothic_mmo_ch1_clean"

if not defined GOTHIC_MMO_MYSQL_URL set "GOTHIC_MMO_MYSQL_URL=mysql://%GOTHIC_MMO_MYSQL_USER%:%GOTHIC_MMO_MYSQL_PASSWORD%@%GOTHIC_MMO_MYSQL_HOST%:%GOTHIC_MMO_MYSQL_PORT%/%GOTHIC_MMO_MYSQL_DATABASE%"
if not defined MYSQL_URL set "MYSQL_URL=%GOTHIC_MMO_MYSQL_URL%"

if not defined MYSQL_EXE if exist "C:\Program Files\MySQL\MySQL Workbench 8.0 CE\mysql.exe" set "MYSQL_EXE=C:\Program Files\MySQL\MySQL Workbench 8.0 CE\mysql.exe"
