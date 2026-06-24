# Data Model Sketch

To nie jest finalna migracja SQL. To szkic mentalny dla LLM/Codexa.

## Core Concepts

`baseline` to stan startowy swiata po deterministic `New Game`.

`delta` to zmiany persistent wzgledem baseline.

`runtime` to stan zywy serwera, ktory nie musi byc zapisywany co tick.

## Suggested Tables

Current local staging implementation:

- `tools/import_world_dump_sqlite.py`
- default wrapper output: `snapshots/tick_*/world_staging.sqlite`
- production-shaped local DB builder: `tools/build_mmo_database.py`
- default MMO DB output: `snapshots/tick_*/gothic_mmo.sqlite`

This SQLite database is a validation/staging target before a real PostgreSQL schema. It currently creates:

- `import_runs`
- `worlds`
- `baseline_entities`
- `baseline_npc_state`
- `baseline_item_state`
- `baseline_inventory_items`
- `baseline_quests`
- `baseline_script_globals`
- `world_delta_events`
- `import_validation`

The importer validates manifest counts for NPCs, items, inventories, mobsi, quests, known dialogs, script globals, and imported event count.

It also builds a first MMO-oriented projection. This is still SQLite and still generated from dumps, but it is deliberately shaped like a server database instead of a file diff:

- `mmo_game_targets`: imported game/content target, e.g. `g2notr`.
- `mmo_world_templates`: immutable baseline world template from deterministic new game.
- `mmo_world_instances`: local shard/state instance built from the latest snapshot.
- `mmo_entity_templates`: baseline NPC/item/mobsi templates.
- `mmo_item_definitions`: item definition samples deduplicated from world and inventory rows.
- `mmo_world_entities`: materialized current state for NPCs, world items, and mobsi.
- `mmo_characters`: player character state extracted from snapshot NPC + stats rows.
- `mmo_inventory`: current inventory for character, NPCs, and containers.
- `mmo_quest_state`: current character quest/topic state.
- `mmo_known_dialog_state`: current known/consumed dialog state.
- `mmo_script_global_state`: current Daedalus global state split into `character` and `world` scope by heuristic.
- `mmo_event_ledger`: normalized event stream with `event_class`.
- `mmo_replay_*`: event-sourced current state rebuilt from baseline plus `mmo_event_ledger`.
- `mmo_replay_validation`: count comparison between raw snapshot projection and replayed canonical state.

Convenience views:

- `v_mmo_event_counts`
- `v_mmo_player_inventory`
- `v_mmo_dead_npcs`
- `v_mmo_delta_killed_npcs`
- `v_mmo_replay_player_inventory`
- `v_mmo_replay_delta`
- `v_mmo_replay_inventory_missing`
- `v_mmo_replay_inventory_extra`
- `v_mmo_character_progress`

This projection is the next step after raw staging. It gives concrete SQL surfaces for MMO questions:

- What is the current state of a shard/world?
- What does a character have equipped or in inventory?
- Which quests/dialog choices/one-shot globals are already consumed?
- Which NPCs are dead and which containers changed?
- Which gameplay events were observed between baseline and snapshot?

### game_targets

- id
- code: `g2notr`, `g1`, `g2`
- version
- content_hash
- created_at

### worlds

- id
- game_target_id
- name
- zen_path
- baseline_hash

### baseline_entities

- id
- world_id
- stable_key
- entity_type: `npc`, `item`, `mob`, `trigger`, `vob`
- source_slot_id
- symbol_index
- script_id
- vob_name
- display_name
- position_x
- position_y
- position_z
- rotation
- raw_json

### baseline_npc_state

- entity_id
- guild
- true_guild
- hp
- hp_max
- mana
- mana_max
- level
- waypoint
- routine_id
- alive
- raw_json

### baseline_item_state

- entity_id
- amount
- item_flags
- material
- value
- visual
- raw_json

### baseline_inventory_items

- owner_entity_id
- item_symbol_index
- item_stable_key
- amount
- equipped
- slot
- raw_json

### world_delta_events

- id
- world_id
- event_type
- entity_stable_key
- actor_character_id
- payload_json
- created_at
- server_tick

Examples:

- `npc_killed`
- `item_removed`
- `item_spawned`
- `container_opened`
- `mob_state_changed`
- `trigger_fired`

### world_entity_state

Materialized current persistent state.

- world_id
- entity_stable_key
- state_json
- updated_at

This can be rebuilt from baseline + events, but a materialized table makes startup faster.

Current SQLite has two equivalents:

- `mmo_world_entities`: raw current state projected directly from the snapshot rows.
- `mmo_replay_entities`: canonical current state rebuilt from baseline + filtered gameplay events.

If default event export hides ambient/runtime noise, `mmo_replay_validation` can show mismatches against raw snapshot counts. That is useful, not automatically a bug: it marks state that exists in save/load but may not belong in canonical MMO persistence.

## Production-Shaped Local MMO DB

`tools/build_mmo_database.py` reads `world_staging.sqlite` and writes a separate server-shaped SQLite database. This is the local precursor to PostgreSQL migrations.

Main table groups:

- account/auth: `account_accounts`, `account_entitlements`
- realm/shards: `realm_realms`, `realm_world_instances`
- content/static data: `content_game_targets`, `content_world_templates`, `content_entity_templates`, `content_item_templates`
- characters: `characters`, `character_stats`, `character_inventory`, `character_quests`, `character_known_dialogs`, `character_script_state`
- normalized items: `item_instances`, `character_equipment`
- persistent world: `world_entity_state`, `world_inventory`, `world_script_state`, `world_event_journal`
- import/debug: `import_audits`, `world_replay_validation`, `world_runtime_noise_candidates`

Useful views:

- `v_character_sheet`
- `v_character_inventory`
- `v_item_instances`
- `v_character_equipment`
- `v_character_item_stacks`
- `v_world_item_stacks`
- `v_world_dead_npcs`
- `v_world_event_counts`
- `v_world_replay_validation`
- `v_runtime_noise_inventory`

Important distinction:

- `world_staging.sqlite` is ETL/debug/reverse-engineering.
- `gothic_mmo.sqlite` is the first server-owned data model. It separates account, realm, content templates, character state, world state, event journal, and runtime noise candidates.

Inventory in `gothic_mmo.sqlite` has two layers:

- import-preserving rows: `character_inventory`, `world_inventory`;
- server-facing rows: `item_instances`, `character_equipment`, stack views.

Do not collapse these too early. Gothic exposes both `amount` and `iterator_count`; equipped items can appear separately from bag rows. Keep source rows until explicit MMO rules decide how gold, consumables, weapons, armor, quest items and equipped slots should behave.

### accounts / players / characters

Keep account data separate from world baseline.

`characters`:

- id
- account_id
- world_id
- name
- position
- direction
- hp/mana
- level/exp/lp
- guild
- last_logout_at

### character_inventory

- character_id
- item_symbol_index
- amount
- equipped
- slot
- item_instance_json

### character_quests

- character_id
- quest_id / topic
- status
- payload_json

Runtime Gothic status mapping:

- `1 = running = in_progress`
- `2 = success = completed_success`
- `3 = failed = completed_failed`
- `4 = obsolete`

In the current SQLite runtime this is exposed through `runtime_quests`,
`runtime_quest_history`, `v_runtime_quest_state`, and
`v_runtime_quest_lifecycle`.

### character_dialogs

- character_id
- npc_symbol_index
- info_symbol_index
- known
- permanent
- condition_symbol_name
- availability_state

Runtime Gothic dialog mapping:

- `known = 1` means the player already heard/selected this info.
- `permanent = 0` and `known = 1` means `consumed_hidden`: it should no longer appear.
- `permanent = 1` and `known = 1` means `repeatable_known`: it can appear again.
- `known = 0` means not consumed yet; availability still depends on condition functions.

Current SQLite tables/views: `runtime_dialog_catalog`,
`runtime_known_dialogs`, `runtime_known_dialog_history`,
`v_runtime_dialog_state`, and `v_runtime_dialog_availability`.

### script_globals

Use carefully. Gothic scripts have many globals; not all belong to global MMO state.

- scope: `world`, `character`, `server`
- key
- value_type
- value
- source_symbol_index

## Runtime vs Persistent

Do persist:

- player character state
- inventory
- quest state
- permanent world changes
- killed unique NPCs
- container contents
- mob/interactable state when gameplay relevant

Do not persist every tick:

- transient AI target
- current animation frame
- temporary perception queue
- short combat cooldowns
- camera
- local client-only state

Persist transient state only if needed for crash recovery, and preferably in short-lived server snapshots, not in canonical DB tables.

## Stable Key Strategy

For baseline import, store all raw identity candidates.

Possible stable key input:

```text
game_target | world | entity_type | symbol_index | vob_name | source_slot_id | rounded_position
```

Hash this into `stable_key`, but keep source columns. If collisions appear, adjust the key with more source fields.
