# Gothic MMO PostgreSQL Production Schema

This folder contains the clean production database contract for the server-authoritative Gothic MMO path.

It is intentionally separate from the runtime SQLite bridge:

- runtime SQLite remains a local capture/restore and save-load parity tool;
- this PostgreSQL schema is the server-owned source of truth for accounts, realms, content revisions, characters, inventory, persistent world state and append-only gameplay events.

## Run on a fresh database

```bash
createdb gothic_mmo
psql "postgresql://USER:PASSWORD@HOST:5432/gothic_mmo" \
  -v ON_ERROR_STOP=1 \
  -f db/migrations/postgres/production/001_gothic_mmo_production_schema.sql
```

Then validate:

```bash
python tools/check_postgres_mmo_schema.py \
  --dsn "postgresql://USER:PASSWORD@HOST:5432/gothic_mmo"
```

## Contract rules

- Do not write gameplay into SQLite `runtime_*` tables from the MMO server.
- Do not use `.sav` blobs as production authority.
- Import/baseline data must be tied to `content_revisions`.
- Every authoritative gameplay mutation must append one row to `world_event_journal` inside the same transaction that updates the current-state projection.
- Use `idempotency_key` for network/client commands so retries cannot duplicate gameplay effects.
- `character_wallets` owns gold/currency; item rows must not double-spend currency.
- Views are read/admin API only. Server writes must target physical tables/functions.
