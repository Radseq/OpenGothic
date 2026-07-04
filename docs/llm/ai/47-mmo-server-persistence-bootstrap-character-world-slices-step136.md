# Step136 - MMO server persistence bootstrap character and world slices

Goal: keep shrinking `mmo_udp_server.cpp` into transport/orchestration code
while the temporary MySQL bridge owns bootstrap read details.

Implemented:
- Added `CharacterBootstrapSnapshotSlices` for character-owned bootstrap data:
  - character list;
  - selected character;
  - inventory;
  - equipment;
  - known dialogs;
  - quests;
  - script integer state.
- Added `WorldBootstrapSnapshotSlices` for simple world/session bootstrap data:
  - world item deltas;
  - world clock;
  - interactive state;
  - NPC lifecycle state;
  - recent events;
  - mover state;
  - trigger queue;
  - world transition state;
  - client corrections;
  - save checkpoint manifest.
- Added persistence readers:
  - `readCharacterBootstrapSnapshotSlices`;
  - `readWorldBootstrapSnapshotSlices`.
- Removed the corresponding SQL construction blocks from `mmo_udp_server.cpp`.

Current behavior:
- No protocol change.
- No DB schema change.
- Bootstrap envelope assembly still happens in `mmo_udp_server.cpp`.
- The biggest remaining bootstrap SQL in UDP is position-centered state:
  active world items and nearby NPC/dialog/waypoint windows.

Why this matters:
- Character and simple world snapshot reads now sit behind typed persistence
  DTOs. Future DB replacement can preserve these contracts while replacing SQL.
- This keeps the current database explicitly temporary and prevents it from
  becoming the shape of future server-authoritative gameplay modules.

Next strong candidates:
- Extract active world item bootstrap as a typed persistence/domain slice.
- Extract nearby NPC/dialog/waypoint bootstrap as a typed persistence/domain
  slice with explicit center/radius input.
- Start moving one gameplay authority from client to server, preferably item
  pickup/drop or interactive use, because those already have persistence paths.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
