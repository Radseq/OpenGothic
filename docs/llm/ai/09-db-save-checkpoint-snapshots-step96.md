# 09 DB Save Checkpoint Snapshots Step96

Purpose: move from "native save exists and DB has a manifest" to "DB contains a
structured save checkpoint that can later be restored without reading `.sav`".

Step96 adds `mmo_create_db_save_checkpoint_v1`. The C++ UDP server calls this
for `save_checkpoint_manifest` actions. The procedure keeps the Step94/95
manifest behavior, then materializes normalized snapshot tables from current DB
projections.

Snapshot domains:
- `mmo_save_checkpoint_character_snapshot`
- `mmo_save_checkpoint_inventory_snapshot`
- `mmo_save_checkpoint_equipment_snapshot`
- `mmo_save_checkpoint_quest_snapshot`
- `mmo_save_checkpoint_known_dialog_snapshot`
- `mmo_save_checkpoint_script_state_snapshot`
- `mmo_save_checkpoint_world_entity_snapshot`
- `mmo_save_checkpoint_world_inventory_snapshot`
- `mmo_save_checkpoint_mover_snapshot`

This is intentionally not a `.sav` blob. It is a save-time copy of authoritative
projections. It is queryable, diffable and suitable for future server restore
work.

Current limits:
- The client still uses normal world/bootstrap materialization after load.
- Step96 does not yet restore from these snapshot tables. It creates the durable
  structured checkpoint that future restore can consume.
- NPC AI queues, animation, particles, camera, sound and transient fight/path
  state remain non-persistent by design.

Validation:

```bash
python3 tools/check_mmo_step96_db_save_checkpoint_snapshots.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --smoke
```

Step97 notes:
- Step97 makes Step96 snapshots part of the actual restore path. When a latest DB-native save checkpoint exists for the active session/character/world, the C++ UDP server exports it as the normal `mmo_bootstrap_snapshot_v1` payload through `mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1`; otherwise it falls back to the current live projection bootstrap.
- The exported snapshot keeps the existing client contract (`character`, `inventory`, `equipment`, `quests`, `known_dialogs`, `script_state`, `active_world_items`, `interactive_state`, `npc_lifecycle_state`, `mover_state`, `server_checkpoint_manifest`) so old server-bound apply code is reused.
- Step97 adds `mmo_save_checkpoint_world_clock_snapshot` and re-wraps `mmo_create_db_save_checkpoint_v1` so new DB save checkpoints also capture server-owned world clock state.
- This is still gated by `-mmo-client-server`; old native `.sav` save/load behavior remains unchanged without server-bound mode.
- The next hard target is strict DB-continue validation: prove that after a save, restart, and bootstrap from DB checkpoint snapshot, bookstand/script/dialog/item changes survive without depending on local `.sav` as authority.

