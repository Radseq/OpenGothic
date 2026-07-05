# MMO server persistence module split - step 140

## What changed

- Split runtime persistence operations out of `server/cpp/mmo_server_persistence.cpp`.
- Added `server/cpp/mmo_server_persistence_direct_ops.cpp` for direct server-authority DB commands:
  checkpointing, outbox, corrections, script/dialog/quest/progression changes,
  combat/resource mutations, triggers, movers, NPC authority state and interactive state.
- Added `server/cpp/mmo_server_persistence_item_ops.cpp` for item/inventory/world-item DB commands.
- Added `server/cpp/mmo_server_persistence_sql.h` for tiny shared SQL formatting helpers that should not leak into higher-level gameplay authority modules.
- Updated `server/cpp/CMakeLists.txt` so the split modules are compiled into `mmo_udp_server`.

## Durable decision

Keep persistence as several focused bridge modules. The current MySQL stored-procedure layer is still temporary and should be replaceable later, so gameplay authority code should depend on narrow persistence functions rather than inline SQL or DB-specific details.

## Verification

- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
- Result: success.

## Next good steps

- Continue splitting `mmo_server_persistence.cpp`; the remaining core is still mostly CLI/process adapter plus bootstrap snapshot readers.
- A good next boundary is a bootstrap/snapshot module, then a low-level MySQL process adapter module.
- Do not expand the public persistence API casually; keep new DB bridge helpers private unless gameplay authority genuinely needs them.
