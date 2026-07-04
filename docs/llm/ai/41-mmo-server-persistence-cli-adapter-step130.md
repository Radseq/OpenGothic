# Step130 - MMO server persistence CLI adapter

Goal: move MySQL CLI execution details out of the UDP server and deeper into
the replaceable persistence bridge.

Implemented:
- `mmo_server_persistence` now owns:
  - MySQL URL parsing via `parseMysqlUrl`;
  - MySQL command construction;
  - shell quoting;
  - `MYSQL_PWD` environment setup;
  - process pipe execution;
  - `runMysql`.
- `mmo_udp_server.cpp` no longer knows how to invoke the `mysql` executable.
  It still calls `runMysql` while the current direct-DB bridge exists.
- Windows environment lookup now uses `_dupenv_s` inside the persistence module,
  removing the previous MSVC `getenv` warning.

Current behavior:
- No protocol change.
- No DB schema change.
- No stored procedure change.
- The server still uses the current MySQL bridge, but the bridge is now more
  isolated from gameplay and packet handling code.

Why this matters:
- The current database is temporary and expected to be redesigned. This step
  reduces the amount of server code coupled to MySQL CLI mechanics.
- Future persistence work should continue moving direct SQL/procedure calls into
  adapter functions with typed request/result structs.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds without the previous `getenv` warning.
