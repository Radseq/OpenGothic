# Step135 - MMO server persistence bootstrap slice extraction

Goal: continue isolating the temporary MySQL bridge from the UDP server before
the eventual database rewrite.

Implemented:
- Moved DB-save-checkpoint bootstrap snapshot restore into
  `buildSaveCheckpointBootstrapSnapshotJson`.
- Added `NpcAuthoritySnapshotSlices` as a typed persistence DTO for bootstrap
  NPC authority slices:
  - routine state;
  - AI state;
  - path state;
  - fight state.
- Added `readNpcAuthoritySnapshotSlices` so live bootstrap and checkpoint
  restore use the same persistence boundary for NPC authority reads.
- Added private persistence JSON field helpers needed to patch checkpoint
  snapshots without depending on UDP-local JSON helpers.
- Removed duplicated NPC authority SQL construction from `mmo_udp_server.cpp`.

Current behavior:
- No protocol change.
- No DB schema change.
- Existing MySQL procedures and current tables are still used.
- `mmo_udp_server.cpp` still builds most of the live bootstrap envelope, but
  save-checkpoint restore and NPC authority slices now come from persistence.

Why this matters:
- Bootstrap state is one of the heaviest places where the server was coupled to
  the current database shape.
- The code now has a small typed contract for NPC authority bootstrap slices.
  A future persistence backend can keep this contract while replacing MySQL SQL.
- This follows the rule that the current database is temporary and should not
  spread deeper into server-authoritative gameplay code.

Next strong candidates:
- Move the remaining live bootstrap snapshot sections behind a persistence
  snapshot builder.
- Split item/world state bootstrap reads into typed slice records.
- Move quest/script direct writes behind typed persistence operations.
- Extract identity/session/world lookup from direct apply paths.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
