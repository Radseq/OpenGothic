# Step133 - MMO server persistence authority operations

Goal: move larger DB operation groups out of `mmo_udp_server.cpp`, using typed
request structs so the current temporary database remains behind the persistence
adapter.

Implemented:
- Added typed persistence records:
  - `OutboxActionRecord`;
  - `CharacterCheckpointRecord`;
  - `SaveCheckpointManifestRecord`;
  - `ClientActionCorrectionRecord`.
- Added persistence operations:
  - `enqueueOutboxAction`;
  - `recordCharacterCheckpoint`;
  - `createSaveCheckpointManifest`;
  - `recordClientActionCorrection`;
  - `readBootstrapReadinessWithFallback`.
- Moved bootstrap readiness/read-model counting and live table fallback out of
  `mmo_udp_server.cpp`.
- Moved SQL/procedure construction for outbox enqueue, character checkpoints,
  save checkpoint manifests and client action corrections out of
  `mmo_udp_server.cpp`.
- Removed stale local UDP helpers that became unused after the extraction.

Current behavior:
- No protocol change.
- No DB schema change.
- Existing MySQL procedures and read-model bridge remain in use.
- Packet parsing still lives in the UDP server, but SQL construction for these
  operations now belongs to persistence.

Why this matters:
- This is a larger move from "helper extraction" to actual operation-boundary
  extraction.
- The current database is expected to be rewritten. These typed records are a
  stepping stone toward replacing MySQL procedure calls with a production
  persistence API without touching movement/world-clock/domain code.
- Future work should continue with high-value operation groups: bootstrap
  snapshot building, world/NPC identity resolution, item transactions and
  script/quest/combat write paths.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
