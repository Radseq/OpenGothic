# 16 DB Checkpoint Export Coverage Step103

Evidence from the first Step102 test:
- strict DB Continue worked: the server exported `snapshot_source=db_save_checkpoint_v1`;
- the client bootstrapped without a native `.sav` and reused the pre-world
  DB checkpoint snapshot;
- gameplay actions before save were clean: `accepted=679`, `unhandled=0`,
  `failed=0`;
- checkpoint table counts were strong: inventory/equipment/story/world/NPC
  domains were present;
- Step102 still reported `no_drift=false` because `script_state` in the full
  checkpoint table was much larger than the hot live projection, and
  `world_clock_rows` was `0`.

Step103 changes the gate:

1. Server and checker raise MySQL session aggregation limits before building
   bootstrap snapshots:

```sql
SET SESSION group_concat_max_len=104857600;
SET SESSION max_execution_time=0;
```

This is needed because DB-save-checkpoint bootstrap export can contain thousands
of script-state rows. A green strict restore must prove that the exported JSON
contains the rows from the snapshot tables, not only that the tables exist.

2. `tools/check_mmo_step103_db_checkpoint_parity.py` replaces Step102 for this
   gate. It reports:
   - checkpoint table counts;
   - live projection counts;
   - exported bootstrap JSON section lengths;
   - `export_coverage` for restore-critical sections;
   - `strict_ready`, which now requires export coverage.

3. `script_state` is excluded from live count drift, because the DB checkpoint
   can intentionally contain the full save/restoration script set while the live
   projection can be a smaller hot subset. It is still checked through
   `export_coverage.script_state`.

4. `server/sql/step103_db_checkpoint_export_coverage.sql` rewraps
   `mmo_materialize_save_checkpoint_world_clock_snapshot_v1` so world clock
   snapshot capture falls back to `realm_world_instances.current_world_time_ms`
   and `current_tick` when `mmo_world_clock_state_current` has no row. New
   checkpoints should no longer show `world_clock_rows=0`.

Use after applying Step103 and making a new real in-game save:

```bash
cd ~/Desktop/OpenGothic

python3 tools/check_mmo_step103_db_checkpoint_parity.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO \
  --assert-strict \
  --assert-export-coverage \
  --output runtime/step103_db_checkpoint_export_coverage/check.json
```

For a strict save -> exit -> continue test with no extra gameplay after save,
also add `--assert-no-drift`. If that fails only on excluded domains, inspect
`count_drift_excluded_domains` before changing runtime code.

