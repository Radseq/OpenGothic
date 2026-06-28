# MySQL Database Ops Backup Manifest

Migration `027_database_ops_backup_manifest.sql` adds production operations metadata:

- `mmo_database_backup_manifests`
- `mmo_database_retention_policies`
- `mmo_record_database_backup_manifest(...)`
- `v_database_backup_manifests`
- `v_database_ops_dashboard`

This does not delete or archive gameplay data. It records evidence that backups/exports exist and defines retention policy intent. `world_event_journal` remains append-only durable gameplay truth.
