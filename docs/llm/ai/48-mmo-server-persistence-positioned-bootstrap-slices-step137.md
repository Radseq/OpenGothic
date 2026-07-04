# Step137 - MMO server persistence positioned bootstrap slices

Goal: remove the last large position-centered bootstrap SQL block from
`mmo_udp_server.cpp` and keep current MySQL details behind persistence.

Implemented:
- Added `PositionedBootstrapSnapshotSlices`:
  - active world items;
  - nearby NPCs;
  - nearby NPC known dialogs;
  - nearby waypoints.
- Added `readPositionedBootstrapSnapshotSlices`.
- Moved active world item source merging from UDP to persistence:
  - `world_inventory`;
  - `world_entity_state`;
  - world inventory read model.
- Moved nearby NPC/dialog/waypoint window queries from UDP to persistence.
- Moved diagnostic logging for active item and nearby NPC bootstrap windows into
  persistence.
- Added private persistence helpers for:
  - active hero bootstrap subquery;
  - ordered JSON array wrapping for source queries.
- Removed now-unused JSON/MySQL helper imports from `mmo_udp_server.cpp`.

Current behavior:
- No protocol change.
- No DB schema change.
- Bootstrap still sends the same snapshot sections.
- `mmo_udp_server.cpp` now asks persistence for character, world, positioned,
  and NPC authority slices, then only assembles the outbound snapshot envelope.

Why this matters:
- The largest bootstrap SQL coupling is now outside the UDP transport layer.
- Future DB replacement can preserve typed slice functions and rewrite only the
  persistence implementation.
- This keeps the current database as an adapter instead of letting its table
  layout define server gameplay code.

Next strong candidates:
- Move remaining direct DB helper paths in `mmo_udp_server.cpp` behind typed
  persistence operations.
- Start server-authoritative gameplay migration with item pickup/drop or
  interactive use.
- Split persistence implementation into smaller files once the API stabilizes:
  session, bootstrap, authority writes, direct apply.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
