# MySQL Production MMO Target

User-selected target: MySQL 8.0+.

This is a parallel production target to the earlier PostgreSQL contract. The current
OpenGothic runtime SQLite database remains a local capture/restore/migration bridge;
MySQL is the server-owned model after bootstrap.

## Files

- `db/migrations/mysql/production/001_gothic_mmo_production_schema.sql`
- `db/migrations/mysql/production/002_bootstrap_import_pipeline.sql`
- `tools/import_runtime_sqlite_to_mysql.py`
- `tools/check_mysql_mmo_schema.py`
- `tools/check_mysql_bootstrap_import.py`

## Ownership split

The MySQL schema keeps the same production ownership boundaries:

- account: `account_accounts`, `account_entitlements`;
- content: `content_game_targets`, `content_revisions`, world/entity/item templates;
- realm/shard: `realm_realms`, `realm_world_instances`;
- character: position, stats, wallet, inventory, equipment, quests, dialogs, script state;
- persistent world: entity state, inventory, script state;
- append-only journal: `world_event_journal`;
- projection/snapshot bookkeeping: `world_projection_offsets`, `world_state_snapshots`;
- import audit: `mmo_import_runs`, `mmo_import_object_map`, `mmo_import_validation_results`.

Do not copy SQLite `runtime_*` tables into MySQL production state. Import only
production projections such as `mmo_*_current` and baseline tables.

## MySQL-specific contract

- UUID columns use `BINARY(16)` with `UUID_TO_BIN(..., 1)` and `BIN_TO_UUID(..., 1)`.
- JSON state uses MySQL `JSON` columns.
- Idempotent event append is exposed as `CALL mmo_append_world_event(..., @mmo_event_id)`.
- Upserts use `ON DUPLICATE KEY UPDATE`.
- MySQL has no PostgreSQL `jsonb`, GIN indexes or partial indexes; equivalents are explicit B-tree keys, generated columns and nullable unique keys.

## Run order

```bash
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo \
  < db/migrations/mysql/production/001_gothic_mmo_production_schema.sql

mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo \
  < db/migrations/mysql/production/002_bootstrap_import_pipeline.sql

python tools/check_mysql_mmo_schema.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo"

python tools/import_runtime_sqlite_to_mysql.py \
  --sqlite runtime/g2notr.sqlite \
  --mysql-url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --realm-key local-dev \
  --account-name local-import \
  --character-key PC_HERO

python tools/check_mysql_bootstrap_import.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --realm-key local-dev \
  --character-key PC_HERO
```

## Next work

After the MySQL bootstrap passes, add the first server write path:

```text
login -> load character -> checkpoint position/stat -> append character_position_checkpoint event -> update current projection in one transaction
```

Do not move to DB-only load until the save/restore parity scenarios are green.
