# 19 MySQL No-Function Checkpoint Export - Step106

Observed after Step105:

- MySQL still returned `ERROR 1419` while creating
  `mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1`.
- On MySQL with binary logging enabled, `CREATE FUNCTION` can require `SUPER`
  or global `log_bin_trust_function_creators=1` even when the routine declares
  deterministic/read-only characteristics.

Step106 rule:

- Do not require local developers to change global MySQL configuration.
- Do not use `CREATE FUNCTION` for DB checkpoint bootstrap export.
- Use a stored procedure with an `OUT` parameter:

```sql
CALL mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(@sid, @snapshot);
```

Step106 changes:

- `server/sql/step104_db_checkpoint_script_state_full_export.sql` now drops the
  old function and creates a procedure with the same routine name.
- `mmo_validate_latest_save_checkpoint_restore_v1` calls the procedure and
  validates the returned snapshot.
- `v_mmo_latest_save_checkpoint_strict_restore` is recreated without calling the
  snapshot export routine, so schema dumps do not fail on an invalid view.
- The C++ UDP server and Step104 checker call the procedure and then read the
  `@snapshot` session variable.

The JSON contract is unchanged: `script_state` remains the safe client-apply
subset, while `script_state_full` remains the full checkpoint coverage export.
