# MySQL Server Write Path

Goal: add the first server-owned write path on top of the MySQL production schema.

This step assumes the database has already been bootstrapped from the runtime SQLite bridge.
An empty MySQL database contains only schema objects; it is not yet usable for character writes
until `tools/import_runtime_sqlite_to_mysql.py` imports `PC_HERO`, the realm, world instances,
positions and stats.

## Files

- `db/migrations/mysql/production/003_server_write_path.sql`
- `tools/check_mysql_server_write_path.py`

## New tables

`server_sessions` is the first server runtime ownership table. It tracks a login session,
account, character, realm, world instance, login/logout events and last-seen timestamp.

`character_checkpoint_audit` records every accepted checkpoint event and the projection row
versions after the write. It is an audit table, not the source of truth. The source of truth is
still the append-only event plus the current-state projection updated in the same transaction.

## New procedures

`mmo_login_character(...)` validates an active account and active character, resolves the current
world instance, appends `character_login`, creates `server_sessions`, and updates
`characters.last_login_at`.

`mmo_checkpoint_character_state(...)` validates an active session, checks the checkpoint
idempotency key, appends `character_position_checkpoint`, updates `character_positions`, updates
`character_stats`, writes audit metadata, and advances the world instance tick.

`mmo_logout_character(...)` appends `character_logout`, closes the session, and updates
`characters.last_logout_at`.

## Idempotency rule

A repeated checkpoint with the same `world_instance_id + idempotency_key` returns the original
event and does not update the projection a second time. This prevents duplicate writes during
network retry. Later mechanics must follow the same pattern: accept one semantic mutation once,
then make retries return the already accepted event.

## Run order

```bash
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo \
  < db/migrations/mysql/production/001_gothic_mmo_production_schema.sql

mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo \
  < db/migrations/mysql/production/002_bootstrap_import_pipeline.sql

mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo \
  < db/migrations/mysql/production/003_server_write_path.sql
```

Then import runtime SQLite:

```bash
python tools/import_runtime_sqlite_to_mysql.py \
  --sqlite runtime/g2notr.sqlite \
  --mysql-url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --realm-key local-dev \
  --account-name local-import \
  --character-key PC_HERO
```

Then smoke-test the server write path:

```bash
python tools/check_mysql_server_write_path.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --account-name local-import \
  --character-key PC_HERO \
  --run-smoke
```

## Next step

After this works, the next mechanic should be wallet/gold. It must use a semantic event such as
`character_wallet_delta` and update `character_wallets` in the same transaction/procedure.
Do not infer gold changes from later inventory diffs.
