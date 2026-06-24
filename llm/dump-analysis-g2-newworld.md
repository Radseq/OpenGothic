# Dump Analysis: G2 `newworld.zen`

Input files:

- `exports/g2/newworld.zen/manifest.json`
- `exports/g2/newworld.zen/npcs.jsonl`
- `exports/g2/newworld.zen/items.jsonl`
- `exports/g2/newworld.zen/npc_inventory.jsonl`

This analysis was made from the first V1 dump. The exporter has since been upgraded to schema `2`, so rerun the dump before using the files as DB baseline input.

## Counts

- NPCs: 1053
- World items: 2461
- NPC inventory rows: 2482
- NPCs with inventory rows: 786
- Dead NPCs at initial state: 3
- NPCs without waypoint: 4, including HERO/player
- Unique NPC symbols: 405
- Unique world item symbols: 204
- Unique inventory item symbols: 162

## Observations

The export is structurally valid JSONL and contains enough data to start a baseline import.

`symbol_index` alone is not a persistent identity. Many NPCs share display names and many item symbols repeat heavily. Examples from the dump:

- `Polna bestia`: 70 NPC rows
- `Owca`: 61 NPC rows
- `Krwiopijca`: 54 NPC rows
- `Roslina lecznicza`: 327 world item rows
- `Ognista pokrzywa`: 179 world item rows
- `Niebieski bez`: 175 world item rows

For world items, `script_id` is currently `0` in the dump, so it cannot identify item instances.

The player row exists as slot `0`, display name `Ja`, with `waypoint: null`. This is expected for the first dump and should be handled specially in DB import.

The three initial dead NPC rows are stone guards:

- slot `722`, symbol `12279`, display `Kamienny straznik`
- slot `723`, symbol `12280`, display `Kamienny straznik`
- slot `724`, symbol `12281`, display `Kamienny straznik`

## Stable Key Result

Tested candidate key inputs:

NPC:

```text
world | slot_id | symbol_index | script_id | display_name | rounded_position | waypoint
```

World item:

```text
world | slot_id | symbol_index | display_name | rounded_position
```

On this dump:

- proposed NPC key duplicate groups: 0
- proposed world item key duplicate groups: 0

This is good enough for schema `2` baseline staging, but production import should keep all raw identity fields so collisions can be diagnosed and the key strategy can be changed later.

## Exporter Changes After Analysis

`WorldStateExporter` schema `2` now adds:

- `stable_key` for NPC rows
- `stable_key` for world item rows
- `stable_key` and `owner_stable_key` for NPC inventory rows
- manifest fields: `game`, `patch`, `item_count`, `npc_inventory_rows`
- target name `g2notr` when `VersionInfo.patch >= 5`

After the next exporter step, schema `2` also writes:

- `mobsi.jsonl`
- `mobsi_inventory.jsonl`

The manifest includes:

- `mobsi_count`
- `mobsi_inventory_rows`

## Next Required Data

To move from baseline staging toward MMO DB, export next:

- trigger state and queued trigger events
- routines/mob routines
- script globals / Daedalus variables, separated into world-scope and player-scope candidates

## Next DB Step

Create an importer that reads schema `2` JSONL and writes staging tables:

- `baseline_manifest`
- `baseline_entities`
- `baseline_npc_state`
- `baseline_item_state`
- `baseline_inventory_items`

Keep `raw_json` for every row until the identity strategy has survived multiple worlds and multiple game targets.
