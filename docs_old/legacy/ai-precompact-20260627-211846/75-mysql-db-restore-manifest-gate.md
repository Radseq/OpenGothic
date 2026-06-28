# MySQL DB Restore Manifest Gate

Migration `026_db_restore_manifest_gate.sql` adds DB-only restore manifests:

- `mmo_db_restore_manifests`
- `mmo_create_db_restore_manifest(...)`
- `v_db_restore_manifests`
- `v_db_restore_manifest_latest`

The procedure materializes a projection hash run, runs the final DB integrity audit and records whether the database projection is DB-ready, externally blocked, or failed.

`manifest_status='blocked_external'` is valid when DB errors are zero but native `.sav` / SQLite / MySQL parity has not been executed yet.
