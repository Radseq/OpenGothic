# MySQL Wallet Write Path

Goal: add the first wallet/gold mechanic after the minimal MySQL server write path.

This step assumes the database already has migrations 001, 002 and 003 applied and has been
bootstrapped from `runtime/g2notr.sqlite`. The source SQLite bridge still records gold both as
an inventory row and as the explicit `runtime_character_wallet` / `mmo_character_wallet_current`
projection. The production server must treat `character_wallets` as the spendable wallet balance
and must not double-spend the source inventory row.

## Files

- `db/migrations/mysql/production/004_wallet_write_path.sql`
- `tools/check_mysql_wallet_write_path.py`

## New table

`character_wallet_audit` records every accepted wallet delta with:

- session;
- character;
- world instance;
- event id;
- idempotency key;
- currency key;
- delta amount;
- amount before/after;
- reason;
- raw JSON payload.

`character_wallets` remains the current-state projection. The append-only source of gameplay
truth is the `character_wallet_delta` event plus deterministic projection.

## New procedures

`mmo_adjust_character_wallet(...)` is the generic wallet mutation procedure.

It validates an active `server_sessions` row, creates a wallet row at zero if missing, locks the
wallet row, rejects negative final balances, appends a `character_wallet_delta` event, updates
`character_wallets`, writes audit metadata, and advances the world tick. It wraps the operation
in an explicit MySQL transaction and rolls back on SQL exceptions.

`mmo_grant_character_gold(...)` and `mmo_spend_character_gold(...)` are convenience wrappers for
`g2notr:gold`. They require a positive amount and internally call `mmo_adjust_character_wallet(...)`
with a positive or negative delta.

## Idempotency rule

A repeated call with the same `world_instance_id + idempotency_key` returns the original wallet
event and the same amount-after value from `character_wallet_audit`. It must not apply the delta
twice. If the idempotency key already belongs to a different event type/class, the procedure fails.

## Run order

```bash
mysql --default-character-set=utf8mb4 -u gothic -p gothic_mmo \
  < db/migrations/mysql/production/004_wallet_write_path.sql
```

Then smoke-test:

```bash
python tools/check_mysql_wallet_write_path.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --account-name local-import \
  --character-key PC_HERO \
  --run-smoke
```

The smoke test logs in, grants 10 gold twice with the same idempotency key, verifies one event and
one audit row, spends 10 gold with a new idempotency key, verifies that the final balance equals
the starting balance, and logs out.

## Next step

After wallet/gold works, the next mechanic should be pickup/remove world item:

```text
active session -> stable world item key -> character inventory/item instance update -> world item state update -> semantic event
```

That step must not infer pickup from a later inventory diff. It should be written through a
single event/projection procedure, similarly to wallet/gold.
