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

Runtime SQLite uruchamiane bezposrednio z gry (`-mmo-sqlite`) ma od schema `12` server-facing projection:

```sql
SELECT * FROM v_mmo_persistence_contract ORDER BY state_domain, view_name;
SELECT * FROM v_mmo_restore_readiness ORDER BY restore_area;
SELECT * FROM v_mmo_character_current;
SELECT * FROM v_mmo_character_stat_sheet;
SELECT * FROM v_mmo_creature_templates ORDER BY spawn_count DESC LIMIT 50;
SELECT * FROM v_mmo_event_journal ORDER BY event_id DESC LIMIT 50;
```

Od schema `18` runtime SQLite ma fizyczne canonical current-state oraz oddzielony immutable world baseline dla calego aktualnie obserwowanego save/load zakresu. Schema `19` dodaje runtimeowy marker delta-capture, aby gra nie przepisywala kompletu EAV statystyk NPC przy kazdym flushu:

```sql
SELECT * FROM mmo_unit_stat_sheet_current WHERE unit_type='character';
SELECT * FROM mmo_unit_stat_current WHERE unit_type='character' ORDER BY display_order;
SELECT * FROM mmo_creature_templates_current ORDER BY spawn_count DESC LIMIT 50;
SELECT * FROM mmo_creature_spawns_current ORDER BY display_name LIMIT 50;
SELECT * FROM mmo_characters_current;
SELECT * FROM mmo_character_inventory_current WHERE character_key='PC_HERO';
SELECT * FROM mmo_character_quests_current WHERE character_key='PC_HERO';
SELECT * FROM mmo_world_items_current WHERE exists_in_world=0;
SELECT * FROM mmo_script_global_values_current ORDER BY global_key, value_index;
SELECT * FROM mmo_world_clock_current;
SELECT * FROM mmo_creature_inventory_snapshots_current WHERE item_row_count=0;
SELECT * FROM mmo_creature_relations_current;
SELECT * FROM v_mmo_world_baseline_status;
SELECT entity_type, entity_ref, short_id, display_name, world_display_name
  FROM v_mmo_world_entity_directory
 ORDER BY entity_type, display_name;
SELECT * FROM v_mmo_world_creature_deltas WHERE delta_kind!='unchanged';
SELECT * FROM v_mmo_world_item_deltas WHERE delta_kind!='unchanged';
```

Ta projekcja jest pomostem miedzy obecnym save/load reverse engineeringiem a docelowym backendem MMO:

- `runtime_*` zostaje warstwa zbierania i diagnostyki OpenGothic.
- `v_mmo_*` jest publicznym kontraktem czytania i audytu dla przyszlego serwera.
- `mmo_*_current` jest materializowanym current-state, ktory ma byc blizszy docelowemu zapisowi serwera niz widoki SQLite.
- Od schema `13` `runtime_npc_stats` jest traktowane jako raw EAV. Poza atrybutami obejmuje progression (kolejny prog XP i LP), stale/tymczasowe nastawienie oraz kompletne `MISSION[]`/`AIVAR[]`, odtwarzane przy restore. Od schema `15` warstwa canonical obejmuje tez `mmo_characters_current`, `mmo_character_inventory_current`, `mmo_character_quests_current`, `mmo_character_known_dialogs_current`, `mmo_world_items_current`, `mmo_world_interactives_current`, `mmo_world_container_inventory_current`, `mmo_script_globals_current`, `mmo_script_global_values_current` i `mmo_guild_attitudes_current`. Schema `16` dodaje `mmo_world_clock_current`, `mmo_creature_inventory_current` oraz marker `mmo_creature_inventory_snapshots_current` dla wszystkich NPC, rowniez pustych inventory. Schema `17` dodaje `mmo_creature_relations_current` dla bezpiecznych checkpointow follow/escort. Schema `18` dodaje `mmo_world_templates`, `mmo_world_instances`, immutable `mmo_world_baseline_*` i widoki delta.
- Schema `19` rozdziela bootstrap od runtime delta write: `runtime_npc_stat_capture_state` przechowuje dokladny podpis komponentu statow NPC. Petla gry skanuje stan silnika, ale odczytuje i zapisuje `runtime_npc_stats` tylko dla NPC, ktorych podpis sie zmienil. Waypoint graph, routine catalog i dialog catalog sa contentem bootstrapowanym na pelnym flushu; snapshoty world/inventory/AI/nawigacji oraz globale stosuja UPSERT lub delete tylko dla rzeczywistych zmian.
- `v_mmo_runtime_npc_navigation` i podobne transient views nie sa docelowa prawda serwera; sluza do crash recovery, testow escort/follow i analizy AI/pathfindingu.
- Restore z DB korzysta z fizycznych tabel `mmo_*_current`: HERO, inventory/equipment, questy, znane dialogi, typed Daedalus globals, nastawienia gildii, dokladny zegar swiata, NPC checkpoint wraz z inventory, follow/escort relation, itemy swiata, mobsi i kontenery. Aktywna kolejka AI/pathfinding pozostaje transient i nie jest wstrzykiwana jako save state.
- Baseline powstaje tylko z `-mmo-sqlite-capture-baseline` w pierwszej sesji swiezo utworzonej DB i musi byc uruchomiony od `New Game`. `content_revision_key` jest obecnie logicznym identyfikatorem runtime; kryptograficzny fingerprint plikow contentu pozostaje kolejnym krokiem przed wieloma realmami.
- Pierwsze uruchomienie po migracji schema `14 -> 15` musi wykonac jeden flush, aby utworzyc normalizowane wartosci globali i macierz nastawien gildii. Wczesniejsze pola sa bootstrapowane z istniejacych `runtime_*`.

Widoki sa nadal potrzebne:

- ukrywaja techniczne raw tables i daja stabilne SQL API dla narzedzi;
- pozwalaja porownac `mmo_*_current` z tym, co wynika z raw capture;
- sa dobrym miejscem na kompatybilnosc podczas migracji do PostgreSQL.

Widoki nie powinny byc docelowym zrodlem prawdy gameplay. Produkcyjna baza MMO powinna miec normalne tabele content/template, spawn/current-state, character state, inventory, quest state i event journal. Widoki moga je laczyc dla odczytu, ale serwer i restore powinny zapisywac/odtwarzac z tabel.

`v_mmo_world_entity_directory` jest przeznaczony dla narzedzi i administratora. Pokazuje czytelne `entity_ref`, np. `g2notr/new-world/npc/1020/11441/297`, krotkie `NPC-1020` oraz `New World`. Surowe `engine_key`, np. `npc:newworld.zen:1020:11441:297`, pozostaje wewnetrznym kluczem restore. Nie wolno opierac relacji lub restore tylko o `display_name`, bo nazwy NPC i itemow nie sa unikalne.

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
