# Gothic MMO MySQL Production Migrations

Apply migrations in numeric order. Steps `023..030` are the final DB-layer completion package.

```bash
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo < db/migrations/mysql/production/023_database_completion_registry.sql
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo < db/migrations/mysql/production/024_projection_hash_manifest.sql
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo < db/migrations/mysql/production/025_final_database_integrity_audit.sql
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo < db/migrations/mysql/production/026_db_restore_manifest_gate.sql
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo < db/migrations/mysql/production/027_database_ops_backup_manifest.sql
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo < db/migrations/mysql/production/028_final_read_models.sql
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo < db/migrations/mysql/production/029_external_integration_gates.sql
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo < db/migrations/mysql/production/030_database_completion_evaluator.sql
```

Validate:

```bash
python3 tools/check_mysql_steps_23_30_database_completion.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --account-name local-import \
  --character-key PC_HERO \
  --run-smoke
```

Expected interpretation after Step 30:

- `database_status=complete`: MySQL DB layer is complete.
- `mmo_status=blocked`: real C++ hooks, parity runner and server-authority layer still need implementation.

Do not mark external gates as passed until real evidence exists.
