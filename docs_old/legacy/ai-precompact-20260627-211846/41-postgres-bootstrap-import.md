# PostgreSQL Bootstrap Import

Goal: seed the production PostgreSQL MMO schema from the current OpenGothic runtime SQLite bridge without making SQLite the production authority.

## Ownership rule

The importer reads SQLite `mmo_*_current` and `mmo_world_baseline_*` projections. It does not copy `runtime_*` diagnostics into production gameplay tables.

SQLite is still useful for capture, restore parity, and migration validation. PostgreSQL owns server state after bootstrap.

## Files

- `db/migrations/postgres/production/002_bootstrap_import_pipeline.sql`
- `tools/import_runtime_sqlite_to_postgres.py`
- `tools/check_postgres_bootstrap_import.py`

## Import path

```text
runtime/g2notr.sqlite
  -> mmo_*_current + mmo_world_baseline_*
  -> content_game_targets
  -> content_revisions
  -> content_world_templates
  -> content_entity_templates
  -> content_item_templates
  -> realm_realms
  -> realm_world_instances
  -> account_accounts/account_entitlements
  -> characters + character_positions + character_stats
  -> character_wallets + character_inventory + selected character_equipment
  -> character_quests + character_known_dialogs + character_script_state
  -> world_entity_state + world_inventory + world_script_state
  -> world_event_journal bootstrap_import_completed
  -> mmo_import_runs + mmo_import_object_map audit rows
```

## Deliberate limitations

The importer is a bootstrap tool, not live replication.

- It should be run against a controlled database or dedicated test realm.
- It is idempotent for the same source fingerprint and keys, but it is not a gameplay merge resolver.
- Equipment is imported only where the SQLite slot is semantically unambiguous: melee weapon slot `1` and ranged weapon slot `2`. Other equipped Gothic rows remain preserved in `character_inventory.raw_payload` until item-template classification is strong enough to map armor, rings, belt, amulet, rune, torch and quick slots safely.
- Currency is written to `character_wallets`; source item rows are preserved for validation but must not become a second spendable gold balance.

## Run order

```bash
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 \
  -f db/migrations/postgres/production/001_gothic_mmo_production_schema.sql

psql "$DATABASE_URL" -v ON_ERROR_STOP=1 \
  -f db/migrations/postgres/production/002_bootstrap_import_pipeline.sql

python tools/import_runtime_sqlite_to_postgres.py \
  --sqlite runtime/g2notr.sqlite \
  --dsn "$DATABASE_URL" \
  --realm-key local-dev \
  --account-name local-import \
  --character-key PC_HERO

python tools/check_postgres_bootstrap_import.py \
  --dsn "$DATABASE_URL" \
  --realm-key local-dev \
  --character-key PC_HERO
```

## Next step

After bootstrap import, add the first server write path:

```text
login -> load character -> checkpoint position/stat -> append character_position_checkpoint event -> update character_positions in the same transaction
```

That write path must use the production event journal, not periodic SQLite diff inference.
