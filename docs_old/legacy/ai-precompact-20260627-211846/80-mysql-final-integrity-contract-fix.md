# MySQL Step 23-30 Final Integrity Contract Fix

## Problem

`mmo_run_final_database_integrity_audit(...)` failed with:

```text
ERROR 1054 (42S22): Unknown column 'c.enabled' in 'where clause'
```

The failing check compared enabled dispatch contracts from `mmo_server_action_dispatch_contracts`
against replay contracts from `mmo_event_projection_contracts`.

`mmo_server_action_dispatch_contracts` has an `enabled` column. `mmo_event_projection_contracts`
does not; replay contracts are registry rows keyed by `event_type` and classed by `event_class`.

## Fix

Migration `025_final_database_integrity_audit.sql` now validates dispatch-to-replay coverage by
matching:

```text
d.event_type = c.event_type
AND d.event_class = c.event_class
```

No nonexistent `c.enabled` predicate is used.

## Apply

Reapply only migration 025 after copying the fixed file:

```bash
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo \
  < db/migrations/mysql/production/025_final_database_integrity_audit.sql
```

Then rerun:

```bash
python3 tools/check_mysql_steps_23_30_database_completion.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --account-name local-import \
  --character-key PC_HERO \
  --run-smoke
```
