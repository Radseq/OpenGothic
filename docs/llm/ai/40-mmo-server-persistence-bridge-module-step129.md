# Step129 - MMO server persistence bridge module

Goal: acknowledge that the current database layer is not the final MMO
persistence design and start isolating it behind a replaceable server module.

Important rule:
- The current MySQL schema, stored procedures and read-model bridge are
  development authority infrastructure. They are useful for current parity work,
  but they are not the final production MMO database.
- Future work should avoid binding gameplay authority modules directly to this
  schema shape.

Implemented:
- Added `server/cpp/mmo_server_persistence.h`.
- Added `server/cpp/mmo_server_persistence.cpp`.
- Registered the new source file in `server/cpp/CMakeLists.txt`.
- Moved DB bridge constants out of `mmo_udp_server.cpp`:
  `DbBridgeVersion` and `MysqlSessionPreamble`.
- Moved basic SQL formatting helpers out of `mmo_udp_server.cpp`:
  `sqlLiteral`, `sqlJson` and `sqlBool`.

Current behavior:
- No protocol change.
- No DB schema change.
- No stored procedure change.
- Existing direct-DB bridge behavior is preserved.

Why this matters:
- The server now has an explicit persistence boundary.
- When the database is redesigned, gameplay modules such as `world_clock` and
  `movement_authority` should not need to be rewritten around old bridge SQL.
- Future extraction should continue in this direction: SQL/procedure calls and
  read-model quirks belong in persistence adapters, while domain modules should
  expose typed requests/results.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
