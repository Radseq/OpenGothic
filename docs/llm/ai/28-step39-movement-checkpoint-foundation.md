# Step39 Movement Checkpoint Foundation

This patch starts Step39 after Step38 trade/combat/resource E2E passed.

## What changed

C++ dev capture:
- `GameSession::tick` can emit bounded periodic player `character_checkpoint` envelopes.
- New flag: `-mmo-action-checkpoint-interval-ms <0|>=250>`.
- Default is `0`, so existing Step37/Step38 captures do not become noisy unless movement checkpoint capture is explicitly enabled.
- The hook emits position, yaw, waypoint key when available, and the player stat sheet required by `mmo_checkpoint_character_state(...)`.

Server-boundary tools:
- `run_mmo_action_receiver.py` normalizes `character_checkpoint` payloads for resolver/direct dispatch diagnostics.
- `run_mmo_resolved_action_worker.py` dispatches `character_checkpoint` to `mmo_checkpoint_character_state(...)`.
- `check_mmo_step39_movement_jsonl.py` validates local JSONL shape, idempotency and optional movement delta.
- `check_mmo_step39_movement_mysql.py` validates outbox, journal, checkpoint audit and current `character_positions` projection.
- `run_mmo_step39_movement_e2e.py` replays captured checkpoints through receiver -> outbox -> worker -> MySQL checker.

## Intentional limits

This is not final MMO movement authority. It is a bounded checkpoint bridge. Final Step39 still needs input/movement proposals, server validation of speed/collision/world bounds, interest management, replication snapshots and reconnect behavior. DB should store checkpoints, not every frame.
