# Step138 - MMO server persistence direct apply simple operations

Goal: continue shrinking `mmo_udp_server.cpp` by moving direct DB procedure
calls that do not need item/NPC resolver materialization behind typed
persistence records.

Implemented:
- Added typed persistence records and functions for:
  - script int updates;
  - quest updates;
  - known dialog updates;
  - progression adjustment;
  - experience reward;
  - character damage;
  - character resource deltas;
  - trigger events;
  - mover state;
  - NPC routine state;
  - NPC AI state;
  - NPC path state;
  - NPC fight state;
  - trigger queue state;
  - world transition state;
  - client correction acknowledgements;
  - interactive use;
  - interactive state updates;
  - NPC weapon state.
- `mmo_udp_server.cpp` now parses packet payloads into typed records for those
  actions and calls persistence instead of assembling `CALL ...` SQL directly.
- Added persistence-side SQL formatting for nullable doubles used by NPC path
  state positions.

Current behavior:
- No protocol change.
- No DB schema change.
- Existing stored procedures are still used.
- Item/inventory direct apply and observed NPC/world item materialization remain
  in `mmo_udp_server.cpp` for now because they depend on resolver and fallback
  materialization logic.

Why this matters:
- A large part of direct apply is now a typed persistence boundary.
- This makes the current database easier to replace later without touching UDP
  packet handling for script/story/progression/world-state observations.
- The remaining direct DB work is now clearer: item transactions, entity
  identity resolution, materialization, and combat writes with resolver fallback.

Next strong candidates:
- Extract item transaction operations behind typed persistence records:
  pickup, remove, equip, unequip, drop, loot, grant fallback.
- Extract world NPC/item identity resolution and observed materialization into
  a domain/persistence adapter.
- Start first true server-authoritative gameplay migration around item pickup
  once item resolution is behind a stable API.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
