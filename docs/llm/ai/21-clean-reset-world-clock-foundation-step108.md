# 21 Clean Reset World Clock Foundation - Step108

Observed after Step107:

- The clean reset correctly skipped legacy Step97/Step98 `CREATE FUNCTION`
  bridges when the Step104/Step106 procedure export path was enabled.
- Reset then failed while applying Step104 with:
  `Table 'gothic_mmo_ch1_clean.mmo_save_checkpoint_world_clock_snapshot' doesn't exist`.
- The schema had procedures and validation/export surfaces that referenced
  `mmo_save_checkpoint_world_clock_snapshot`, but the final procedure-only path
  did not create that table from a clean database.

Step108 rule:

- The procedure-only DB checkpoint path must be self-contained. It must not rely
  on legacy Step97 just to create world-clock checkpoint storage.

Step108 changes:

- Adds `server/sql/step108_db_checkpoint_world_clock_foundation.sql`.
- Creates `mmo_save_checkpoint_world_clock_snapshot` if missing.
- Recreates `mmo_materialize_save_checkpoint_world_clock_snapshot_v1` with the
  Step103 fallback from `realm_world_instances` when the hot world-clock current
  table has no row.
- Recreates `mmo_create_db_save_checkpoint_v1` so every DB save checkpoint
  materializes both the main snapshot and the world-clock snapshot.
- `reset_mmo_mysql_from_chapter1_start.py` applies Step108 before Step103/104
  so Step104 can create views/procedures safely, and applies it again after
  Step104 so the final `mmo_create_db_save_checkpoint_v1` contract is retained.
- `apply_mmo_step104_db_checkpoint_script_state_full_export.py` applies Step108
  before and after Step104 for existing databases.

Manual clean reset remains:

```bash
python3 tools/run_mmo_step55_clean_mysql_from_pre_xardas.py \
  --sqlite runtime/g2notr_ch1_pre_xardas.sqlite \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --i-understand-this-drops-database
```

Expected manifest evidence:

- Step97/Step98 entries are still
  `skipped_replaced_by_step104_procedure_export`.
- `server/sql/step108_db_checkpoint_world_clock_foundation.sql` appears twice:
  once with `phase=before_step103_step104`, once with `phase=after_step104`.
- Clean reset should no longer fail with missing
  `mmo_save_checkpoint_world_clock_snapshot`.
