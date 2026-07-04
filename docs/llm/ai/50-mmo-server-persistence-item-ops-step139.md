# Step139 - MMO server persistence item operation extraction

Goal: move item/inventory direct apply stored procedure calls out of
`mmo_udp_server.cpp` while leaving resolver/materialization logic untouched for
a later domain extraction.

Implemented:
- Added typed persistence records and functions for:
  - `transferCharacterItem`;
  - `lootWorldInventoryItem`;
  - `grantCharacterItemBySymbol`;
  - `pickupWorldItem`;
  - `removeWorldItem`;
  - `equipCharacterItem`;
  - `unequipCharacterItem`;
  - `dropCharacterItem`.
- Added typed persistence records and functions for resolved combat mutations:
  - `applyWorldEntityDamage`;
  - `markNpcDead`.
- Moved item operation `CALL mmo_*` SQL formatting from `mmo_udp_server.cpp`
  into `mmo_server_persistence.cpp`.
- Added typed nullable drop position fields using `std::optional<double>`.
- Removed the now-dead final `runMysql(sql)` path from `applyDirectDb`; direct
  apply branches now return after calling typed persistence operations.

Current behavior:
- No protocol change.
- No DB schema change.
- Existing MySQL stored procedures remain the bridge.
- `mmo_udp_server.cpp` still owns item/NPC resolver and observed materialization
  SQL. That logic is coupled to payload parsing and fallback behavior and should
  be extracted as a separate domain adapter.

Why this matters:
- Normal item mutations are now behind a typed persistence boundary.
- UDP transport is much closer to orchestration only.
- The remaining SQL in UDP is now mostly identity/materialization support, not
  direct gameplay mutation calls.

Next strong candidates:
- Extract observed NPC/world item materialization into a persistence/domain
  adapter.
- Extract item and NPC resolver queries behind stable APIs.
- Start the first true server-authoritative item pickup/drop validation once
  resolver/materialization is behind a clean boundary.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds without warnings.
