# Step39 Coalesced Movement Evidence

This pass hardens the first Step39 movement/checkpoint implementation after the initial E2E passed.

## C++ changes

- Added checkpoint coalescing command-line controls:
  - `-mmo-action-checkpoint-min-distance <world-units>`
  - `-mmo-action-checkpoint-min-yaw-deg <degrees>`
  - `-mmo-action-checkpoint-force-interval-ms <ms>`
- `GameSession::tick` now compares the current player checkpoint against the last emitted checkpoint.
- A checkpoint is emitted only for:
  - first checkpoint,
  - configured interval with no coalescing,
  - distance delta,
  - yaw delta,
  - stat/resource/progression delta,
  - forced keepalive interval.
- The emitted payload includes `reason`, cadence and coalescing settings for later evidence analysis.

## Tooling changes

- `check_mmo_step39_movement_jsonl.py` now reports movement distance, stationary ratio, tick deltas, bounding box and checkpoint reason distribution.
- `run_mmo_step39_movement_e2e.py` can coalesce old/noisy captures before replaying them to the receiver.
- `check_mmo_step39_movement_mysql.py` verifies outbox, journal, audit, distinct positions and latest projection/audit consistency.
- `build_mmo_step39_movement_manifest.py` creates a compact evidence manifest with hashes and pass/fail status.

## Boundary

This remains bounded checkpoint persistence. It intentionally avoids writing every frame to MySQL. The next production movement work is not more checkpoint spam; it is server-side movement proposal validation, world-bound/collision checks, interest management and replication snapshots.

## Step39 v2 compile strictness fix

The checkpoint state now stores `guild` as `uint32_t`, matching `Npc::guild()`.
This keeps the coalescing comparison and state snapshot warning-clean under the
project's `-Wconversion -Werror` build flags, without weakening compiler checks.
`trueGuild` remains signed because `Npc::trueGuild()` returns `int32_t`.

