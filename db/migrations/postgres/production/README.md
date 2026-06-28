# Gothic MMO PostgreSQL Production Schema

This folder contains the clean production database contract for the server-authoritative Gothic MMO path.

It is intentionally separate from the runtime SQLite bridge:

- runtime SQLite remains a local capture/restore and save-load parity tool;
- this PostgreSQL schema is the server-owned source of truth for accounts, realms, content revisions, characters, inventory, persistent world state and append-only gameplay events.

## Apply migrations on a fresh database

```bash
createdb gothic_mmo
psql "postgresql://USER:PASSWORD@HOST:5432/gothic_mmo" \
  -v ON_ERROR_STOP=1 \
  -f db/migrations/postgres/production/001_gothic_mmo_production_schema.sql

psql "postgresql://USER:PASSWORD@HOST:5432/gothic_mmo" \
  -v ON_ERROR_STOP=1 \
  -f db/migrations/postgres/production/002_bootstrap_import_pipeline.sql
```

Then validate the schema:

```bash
python tools/check_postgres_mmo_schema.py \
  --dsn "postgresql://USER:PASSWORD@HOST:5432/gothic_mmo"
```

## Bootstrap from runtime SQLite

The importer reads production-facing SQLite projections such as `mmo_*_current` and `mmo_world_baseline_*`. It does not copy `runtime_*` diagnostics into production tables.

Generate SQL first:

```bash
python tools/import_runtime_sqlite_to_postgres.py \
  --sqlite runtime/g2notr.sqlite \
  --dry-run-sql /tmp/import_g2notr.sql
```

Apply directly:

```bash
python tools/import_runtime_sqlite_to_postgres.py \
  --sqlite runtime/g2notr.sqlite \
  --dsn "postgresql://USER:PASSWORD@HOST:5432/gothic_mmo" \
  --realm-key local-dev \
  --account-name local-import \
  --character-key PC_HERO
```

Validate imported bootstrap data:

```bash
python tools/check_postgres_bootstrap_import.py \
  --dsn "postgresql://USER:PASSWORD@HOST:5432/gothic_mmo" \
  --realm-key local-dev \
  --character-key PC_HERO
```

## Contract rules

- Do not write gameplay into SQLite `runtime_*` tables from the MMO server.
- Do not use `.sav` blobs as production authority.
- Import/baseline data must be tied to `content_revisions`.
- Every authoritative gameplay mutation must append one row to `world_event_journal` inside the same transaction that updates the current-state projection.
- Use `idempotency_key` for network/client commands so retries cannot duplicate gameplay effects.
- `character_wallets` owns gold/currency; item rows must not double-spend currency.
- Views are read/admin API only. Server writes must target physical tables/functions.
- Import audit tables are allowed in production, but raw runtime diagnostics are not production gameplay state.
