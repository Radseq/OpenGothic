# 10 DB Save Checkpoint Restore Step97

Purpose: turn the Step96 DB-native save checkpoint tables into a real restore source for server-bound bootstrap.

Step96 created normalized snapshot tables at save time. Step97 adds the missing read path: if the active server session has a latest DB save checkpoint, the C++ UDP server exports that checkpoint as the existing `mmo_bootstrap_snapshot_v1` JSON payload. The client does not need a new per-domain apply path; it reuses the normal server-bound snapshot materialization.

Implemented surfaces:
- `mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(session_id)` returns a bootstrap-compatible JSON snapshot from the latest DB save checkpoint.
- `mmo_create_db_save_checkpoint_v1` is rewrapped so new save checkpoints also capture `mmo_save_checkpoint_world_clock_snapshot`.
- `v_mmo_latest_save_checkpoint_restore_readiness` reports whether the latest checkpoint has character, inventory, quest/dialog/script, world entity, world inventory, world clock and mover rows plus exported bootstrap byte size.
- `mmo_udp_server` tries the DB save checkpoint export first and falls back to live current projections only when no checkpoint exists or the export function is unavailable.

Why this matters:
- `.sav` is now closer to a compatibility/debug cache.
- DB checkpoint snapshots become a boot materialization source, not only an audit artifact.
- The user scenario “talk to Xardas, pick up items, read bookstand, save, restart” can now be tested against DB checkpoint restore instead of only live projections.

Validation:

```bash
python3 tools/check_mmo_step97_db_save_checkpoint_restore_bridge.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --smoke \
  --output runtime/step97_db_save_checkpoint_restore_bridge/check.json
```

After an actual in-game save, inspect:

```sql
SELECT *
FROM v_mmo_latest_save_checkpoint_restore_readiness
ORDER BY created_at DESC
LIMIT 10;
```

Expected server log after a saved checkpoint exists:

```text
[bootstrap_db_save_checkpoint_restore] bytes=... session=...
bootstrap_snapshot_sent ...
```

Limits:
- This is still load-time materialization, not live replication.
- NPC AI/path queues, animation, camera, particles and audio are intentionally not persisted.
- If no DB save checkpoint exists yet, the bootstrap falls back to current projections so New Game reset remains usable.
