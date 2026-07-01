# 07 Save To Server Step93 Foundation

This step implements the first practical chunk of the save-to-server roadmap
without rewriting the old save system.

Scope:
- additive server-bound behavior only;
- old `.sav` New Game/Load Game/Save Game remains the fallback/debug path;
- the C++ server and MySQL procedures gain durable surfaces that help replace
  save-file state with DB/server truth over time.

Implemented surfaces:

1. Quest UTF-8/idempotency bridge
   - `server/sql/step93_save_checkpoint_quest_utf8_bridge.sql` creates
     `character_quest_audit` and replaces `mmo_update_character_quest`.
   - The procedure accepts utf8mb4 quest keys/names, normalizes invalid status
     to `running`, journals `character_quest_updated` as event class `quest`,
     upserts `character_quests`, and returns the existing event for repeated
     idempotency keys.
   - This addresses failures around Polish quest text and repeated client/server
     replay of the same semantic quest update.

2. Server checkpoint manifest in snapshot
   - `mmo_bootstrap_snapshot_v1` now includes `server_checkpoint_manifest`.
   - The manifest summarizes the current session, character, world, latest
     checkpoint tick, inventory/equipment/quest/dialog/script/world/interactives
     row counts and recent event sequence.
   - It is evidence/readiness metadata, not gameplay state by itself.

3. Mover state in snapshot
   - `mmo_bootstrap_snapshot_v1` now includes optional `mover_state` from
     `mmo_world_mover_state_current` when the Step51 mover bridge exists.
   - The SQL is fail-soft. Missing mover tables must produce diagnostics and an
     empty section, not abort snapshot delivery.

4. Client JSON string decoding
   - Server-bound snapshot parsing now decodes JSON string escapes, including
     `\uXXXX` and surrogate pairs.
   - Quest entries, dialog keys, entity keys and display labels are restored as
     UTF-8 strings before local materialization/logging.

Not implemented yet:
- direct application of mover frame/state into live `MoveTrigger` objects;
- baseline-ZEN-only load without a native `.sav` bootstrap file;
- server-side NPC routines/AI/pathing;
- rollback/correction of rejected client mutations.

Next safe C++ target:
- add a narrow `MoveTrigger`/world restore API that can set persisted mover
  frame/state under `-mmo-client-server` without executing local trigger events;
- then apply `mover_state` after world load the same way interactive state is
  currently restored.
