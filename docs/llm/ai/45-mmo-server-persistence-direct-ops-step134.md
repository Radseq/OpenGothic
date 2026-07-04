# Step134 - MMO server persistence direct operation expansion

Goal: continue the larger persistence extraction so `mmo_udp_server.cpp` routes
packets while the temporary MySQL bridge owns DB operation details.

Implemented:
- Added typed persistence records for direct authority writes:
  - `OutboxActionRecord`;
  - `CharacterCheckpointRecord`;
  - `SaveCheckpointManifestRecord`;
  - `ClientActionCorrectionRecord`.
- Added persistence operation functions:
  - `enqueueOutboxAction`;
  - `recordCharacterCheckpoint`;
  - `createSaveCheckpointManifest`;
  - `recordClientActionCorrection`.
- Moved bootstrap readiness read-model counting and live table fallback into
  `readBootstrapReadinessWithFallback`.
- `mmo_udp_server.cpp` no longer constructs SQL for:
  - outbox enqueue;
  - character checkpoint;
  - movement checkpoint;
  - save checkpoint manifest;
  - client correction;
  - bootstrap readiness checks.
- Added explicit `<initializer_list>` include for the persistence header.

Current behavior:
- No protocol change.
- No DB schema change.
- Existing MySQL procedures are still used.
- UDP packet parsing and semantic routing remain in `mmo_udp_server.cpp`.

Why this matters:
- This is a larger extraction of whole DB operation groups, not just helper
  movement.
- The current DB remains a temporary bridge. More SQL is now isolated behind
  typed persistence functions, making the eventual DB rewrite less invasive.

Next strong candidates:
- Move bootstrap snapshot building into a typed persistence snapshot builder.
- Split world/NPC identity resolution from the UDP packet loop.
- Move item transaction SQL behind owner-aware persistence/domain records.
- Move script/quest/combat direct write paths behind typed persistence records.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
