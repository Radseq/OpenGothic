# 18 MySQL Function Binary-Log Safety - Step105

Observed failure:

```text
ERROR 1419 (HY000): You do not have the SUPER privilege and binary logging is enabled
```

This happened while applying the Step104 function
`mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1`.

Rule:

- Do not require developers to change global MySQL settings such as
  `log_bin_trust_function_creators`.
- Stored functions installed by local/dev bridge SQL must declare the required
  function characteristics explicitly.
- For read-only snapshot/export functions use:

```sql
RETURNS longtext CHARSET utf8mb4
    DETERMINISTIC
    READS SQL DATA
```

Step105 changes:

- The Step104 script-state-full export function is now declared
  `DETERMINISTIC READS SQL DATA`, so it can be created on MySQL installs with
  binary logging enabled and without SUPER.
- `apply_mmo_step104_db_checkpoint_script_state_full_export.py` also makes
  `CREATE FUNCTION` headers binlog-safe before sending SQL to MySQL. This keeps
  the installer tolerant of mixed local checkouts where the SQL file was not yet
  updated.
- `run_mmo_step55_clean_mysql_from_pre_xardas.py` still remains the single
  clean DB entry point. It now calls the reset module in-process instead of
  spawning `reset_mmo_mysql_from_chapter1_start.py` as a separate Python
  process. The reset still uses MySQL/import/check commands internally because
  those are the real external tools.
- Clean DB applies Step103 and Step104 by default. Manual `apply_*` scripts are
  only for patching an existing DB without dropping it.
