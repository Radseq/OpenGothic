# Step131 - MMO server persistence result adapter

Goal: keep moving MySQL bridge mechanics out of `mmo_udp_server.cpp` so future
database replacement can happen behind a narrower adapter boundary.

Implemented:
- `mmo_server_persistence` now owns:
  - `splitMysqlLastRow`;
  - `mysqlSingleField`;
  - `mysqlSingleFieldWithDiagnostic`;
  - `mysqlJsonOr`;
  - `mysqlJsonOrWithDiagnostic`;
  - `concatenateJsonArrays`.
- `mmo_udp_server.cpp` still builds many bridge SQL statements, but no longer
  parses raw MySQL CLI rows or applies JSON fallback behavior locally.

Current behavior:
- No protocol change.
- No DB schema change.
- No stored procedure change.
- Snapshot JSON output should remain unchanged.

Why this matters:
- MySQL CLI output shape is now persistence-adapter knowledge, not packet-loop
  knowledge.
- The next persistence extraction should move whole procedure/query operations
  into typed functions instead of only helper utilities.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
