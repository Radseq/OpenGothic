# Production MMO Database Roadmap

Goal: move from the current hybrid OpenGothic save/load + runtime SQLite bridge to a server-authoritative database without losing `.sav` compatibility while restore parity is still being proven.

## Current rule

Keep the hybrid flow for now:

```text
.sav    = bootstrap, compatibility path, backup and parity oracle
SQLite  = local capture/restore bridge and migration validator
Postgres = production server source of truth
```

SQLite remains useful, but it is not the final MMO database contract.

## Step 1: Freeze the PostgreSQL production contract

Status: implemented by `db/migrations/postgres/production/001_gothic_mmo_production_schema.sql`.

Scope:

- account and entitlement ownership;
- content revisions and immutable world/item/entity templates;
- realm and world instances;
- character state, stats, wallet, inventory, equipment, quests, dialogs and script state;
- persistent world entity state, container/world inventory and world script state;
- append-only `world_event_journal` with idempotency keys;
- projection offsets and state snapshots;
- read/admin views;
- smoke validator: `tools/check_postgres_mmo_schema.py`.

Hard rule: gameplay mutations must not be inferred from periodic full-world diffs once the server owns writes. A successful gameplay operation must append one semantic event and update the current-state projection in the same transaction.

## Step 2: Build PostgreSQL bootstrap/import pipeline

Input sources:

- current runtime SQLite `mmo_*_current` and `mmo_world_baseline_*` tables;
- deterministic new-game baseline export;
- save-slot snapshots only as bridge data.

Output:

- `content_game_targets`;
- `content_revisions`;
- `content_world_templates`;
- `content_entity_templates`;
- `content_item_templates`;
- one test `realm_realms` row;
- one `realm_world_instances` row per imported world;
- one test `account_accounts` and `characters` row for `PC_HERO`.

Do not import `runtime_*` raw diagnostics as production gameplay tables.

## Step 3: Add a minimal server write path

First vertical slice:

```text
login -> load character -> enter world -> checkpoint position/stat -> append event -> update projection -> logout
```

Required operations:

- read character sheet from PostgreSQL;
- append `character_position_checkpoint` event through `mmo_append_world_event`;
- update `character_positions` in the same transaction;
- verify idempotent retry does not duplicate the event.

## Step 4: Add replay/projection validator

The server must be able to rebuild current state from:

```text
content baseline + world_event_journal
```

Current-state projection tables may exist for speed, but they must be reproducible.

## Step 5: Move gameplay mechanics one by one

Order:

1. character position/stat checkpoint;
2. wallet/gold;
3. pickup/remove world item;
4. character inventory and equipment;
5. container inventory and interactive state;
6. quest/dialog/script progress;
7. NPC death/respawn rules;
8. trade;
9. combat/spells.

Each mechanic needs a semantic event type and a deterministic projection rule.

## Step 6: Strict restore parity gate

Before removing `.sav` from any flow, run scenario tests:

- read bookstand / one-shot script global / exp gain;
- pickup item;
- equip/unequip;
- chest/container change;
- quest progress;
- dialog consumed;
- NPC killed;
- chapter change;
- save, restart, load, compare native `.sav` state with database-restored state.

A difference is allowed only if it is explicitly classified as transient presentation/runtime noise.

## Step 7: SQLite-only or DB-only load

Only after Step 6 is clean:

- add strict DB restore mode;
- fail loudly if no matching DB snapshot/projection exists;
- then add standalone DB load.

Do not silently fall back to stale `.sav` in strict tests.
