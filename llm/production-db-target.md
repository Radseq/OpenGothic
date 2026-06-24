# Production DB Target

Cel koncowy: baza serwera MMO, nie save game w tabeli.

Inspiracja architektoniczna jest podobna do duzych MMO:

- auth/account database: konta, entitlements, bany, sesje;
- realm database: realmy/shardy, status serwerow, world instances;
- world/content database: statyczne template'y swiata, NPC, itemy, mobsi, quest/dialog metadata;
- character database: postacie, staty, inventory, questy, znane dialogi, character-scoped script state;
- persistent world database: zabite unikalne NPC, zabrane/spawnowane itemy, kontenery, mobsi/interactables, world-scoped script state;
- event journal: append-only ledger zmian gameplay, z ktorego mozna odbudowac canonical state;
- runtime/cache poza baza: pozycje graczy per tick, AI transient state, animacje, perception queues.

## Current Local Artifact

`tools/build_mmo_database.py` buduje lokalny odpowiednik przyszlego PostgreSQL:

```text
exports/g2notr/newworld.zen/snapshots/tick_*/gothic_mmo.sqlite
```

PostgreSQL schema contract:

```text
db/migrations/postgres/001_gothic_mmo_schema.sql
```

Local smoke test / invariant checker:

```text
tools/check_mmo_database.py
```

Ta baza jest zasilana z:

```text
world_staging.sqlite -> mmo_replay_* -> gothic_mmo.sqlite
```

Czyli finalny local DB nie kopiuje raw snapshotu. Uzywa canonical replay z baseline + eventow.

## Table Groups In gothic_mmo.sqlite

Account:

- `account_accounts`
- `account_entitlements`

Realm:

- `realm_realms`
- `realm_world_instances`

Content:

- `content_game_targets`
- `content_world_templates`
- `content_entity_templates`
- `content_item_templates`

Characters:

- `characters`
- `character_stats`
- `character_inventory`
- `item_instances`
- `character_equipment`
- `content_item_classification`
- `character_quests`
- `character_known_dialogs`
- `character_script_state`

Persistent world:

- `world_entity_state`
- `world_inventory`
- `world_script_state`
- `world_event_journal`
- `world_replay_validation`
- `world_runtime_noise_candidates`

## Useful Queries

```sql
SELECT * FROM v_character_sheet;
SELECT * FROM v_character_inventory ORDER BY item_display_name;
SELECT * FROM v_item_instances WHERE owner_type = 'character';
SELECT * FROM v_character_equipment;
SELECT * FROM v_item_class_counts ORDER BY template_count DESC;
SELECT * FROM v_character_item_totals ORDER BY item_display_name;
SELECT * FROM v_character_item_stacks ORDER BY item_display_name;
SELECT * FROM v_character_stack_policy_issues;
SELECT * FROM v_world_item_stacks LIMIT 30;
SELECT * FROM v_character_inventory_anomalies;
SELECT * FROM v_world_dead_npcs;
SELECT * FROM v_world_event_counts ORDER BY event_count DESC;
SELECT * FROM v_world_replay_validation;
SELECT * FROM v_runtime_noise_inventory LIMIT 30;
```

Smoke test:

```powershell
python tools/check_mmo_database.py --db exports/g2notr/newworld.zen/snapshots/tick_*/gothic_mmo.sqlite
```

If known item policy issues are expected during reverse engineering:

```powershell
python tools/check_mmo_database.py --db exports/g2notr/newworld.zen/snapshots/tick_*/gothic_mmo.sqlite --allow-known-policy-issues
```

Inventory diagnostics are intentionally conservative. Gothic can represent item stacks, equipped items, and multiple item instances in ways that are not yet fully normalized. `v_character_inventory_anomalies` is not an automatic error list; it is a work queue for deciding server semantics:

- `equipped_and_bag_split`: same item template appears as equipped and also in bag.
- `duplicate_item_rows`: same item template appears in multiple rows.
- `amount_exceeds_iterator_count`: exported amount is greater than iterator count and may need stack normalization.

## Inventory Normalization Layer

The importer keeps legacy/import rows:

- `character_inventory`
- `world_inventory`

Then it materializes server-facing rows:

- `item_instances`: one durable row per imported item instance/stack row.
- `character_equipment`: equipped item instances by character and slot.
- `content_item_classification`: heuristic server class and stack policy for item templates.
- `v_character_item_stacks`: aggregate per character/item template.
- `v_world_item_stacks`: aggregate per world container/NPC/item template.
- `v_character_stack_policy_issues`: rows that violate current stack/equipment policy.

This split is important for MMO persistence:

- gold and consumables can be stack-like;
- weapons and armor may need durable instances;
- equipped state must not be inferred from a bag row;
- quest items may need special binding/rules later;
- Gothic export fields `amount` and `iterator_count` can differ, so both are preserved until server rules are explicit.

Current item classes are heuristic and versioned by `rule_version`:

- `currency`
- `equipment_weapon_melee`
- `equipment_weapon_ranged`
- `equipment_armor`
- `equipment_accessory`
- `ammo`
- `consumable_food`
- `consumable_potion`
- `consumable_plant`
- `spell_scroll`
- `readable`
- `key`
- `quest_or_progression_item`
- `quest_or_script_item`
- `crafting_or_currency_material`
- `trade_good`
- `misc`

Current stack policies:

- `stack`: stackable quantity, e.g. gold, food, potions, scrolls.
- `instance`: durable item instance, e.g. weapons, armor, accessories.
- `unique`: one-off/key/progression item candidate.

## Next Production Steps

1. Convert `gothic_mmo.sqlite` schema to PostgreSQL migrations.
2. Add migration versioning and automated migration smoke test.
3. Add importer that writes directly into PostgreSQL.
4. Add server prototype that reads `characters`, writes character position/stat changes, and appends to `world_event_journal`.
5. Add periodic snapshots derived from event replay.
6. Add repair/admin tools for replay mismatches and runtime-noise classification.
