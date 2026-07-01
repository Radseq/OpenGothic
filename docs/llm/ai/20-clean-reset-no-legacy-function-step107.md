# 20 Clean Reset Without Legacy Function Bridges - Step107

Observed after Step106:

- Existing DB patching works and the C++ server can export DB checkpoint
  bootstrap snapshots through the procedure-only Step106 path.
- Destructive clean reset still failed before reaching Step104/Step106 because
  it applied old Step97/Step98 SQL that used `CREATE FUNCTION`.
- MySQL with binary logging enabled can reject `CREATE FUNCTION` for normal dev
  users with `ERROR 1419`.

Step107 rule:

- When Step104/Step106 procedure export is enabled, clean reset must not apply
  legacy Step97/Step98 function-based restore bridges.
- Step96 still installs normalized checkpoint snapshot tables.
- Step103 still installs world-clock checkpoint fallback materialization.
- Step104/Step106 installs the final procedure-based checkpoint export and
  strict validation surfaces.

Step107 changes:

- `reset_mmo_mysql_from_chapter1_start.py` skips Step97 and Step98 SQL when
  Step104 checkpoint script-state/full export is enabled.
- The reset manifest records those skips as
  `skipped_replaced_by_step104_procedure_export`.
- `run_mmo_step55_clean_mysql_from_pre_xardas.py` registers the dynamically
  loaded reset module in `sys.modules`, which avoids Python 3.14 dataclass
  import crashes during in-process reset.
