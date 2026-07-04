# Step128 - MMO server movement authority module

Goal: continue splitting the C++ UDP server into narrow authority modules by
moving movement proposal validation out of `mmo_udp_server.cpp`.

Implemented:
- Added `server/cpp/mmo_server_movement_authority.h`.
- Added `server/cpp/mmo_server_movement_authority.cpp`.
- Registered the new source file in `server/cpp/CMakeLists.txt`.
- Moved movement validation thresholds and pure validation math into
  `Mmo::Server::validateMovementProposal`.
- Kept DB checkpoint writes, packet parsing, correction recording and ACK/NACK
  wiring in `mmo_udp_server.cpp`.

Current behavior:
- No protocol change.
- No DB schema change.
- Accepted/rejected movement decisions use the same thresholds as before:
  coordinate bounds, delta window, max step distance, horizontal speed, vertical
  speed and fall tolerance.
- Rejection and stale-small-delta logs preserve the same fields.

Why this matters:
- Movement authority is now a reusable server domain service instead of inline
  UDP handler code.
- Future correction/reconciliation work can expand this module without growing
  the packet loop.
- This is the natural home for later world-bounds checks, simplified collision,
  anti-teleport policy and typed movement correction deltas.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
