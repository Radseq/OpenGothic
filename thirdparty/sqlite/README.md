# Local SQLite for Windows

This directory is used by OpenGothic's runtime MMO SQLite mode when CMake cannot find a system SQLite package.

Expected layout:

```text
thirdparty/sqlite/include/sqlite3.h
thirdparty/sqlite/lib/sqlite3.lib
thirdparty/sqlite/bin/sqlite3.dll
```

Official downloads:

- SQLite download page: https://www.sqlite.org/download.html
- Windows DLL package: `sqlite-dll-win-x64-*.zip`
- Source/header package: `sqlite-amalgamation-*.zip`

If the DLL package contains `sqlite3.def` but not `sqlite3.lib`, generate the import library from a Visual Studio Developer Command Prompt:

```cmd
mkdir thirdparty\sqlite\lib
lib /def:thirdparty\sqlite\bin\sqlite3.def /machine:x64 /out:thirdparty\sqlite\lib\sqlite3.lib
```

Then rerun CMake/build. On Windows, CMake will link `sqlite3.lib` and copy `sqlite3.dll` next to `Gothic2Notr.exe`.
