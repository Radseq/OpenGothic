# State Delta Events

The MMO database must model operations that change state, not only initial world state.

Important gameplay operations to detect:

- player picks up a world item
- player drops an item
- player eats food or uses a plant/potion
- player equips or unequips weapon/armor
- player shoots an arrow/bolt
- player casts a spell
- player buys, sells, or trades an item
- player loots a container
- player opens, closes, locks, or unlocks a door/chest
- player talks to an NPC
- quest log changes
- script global changes
- NPC changes AI/dialog/combat state
- NPC dies, is knocked down, or respawns

First implementation path:

```text
initial dump -> play -> save dump -> compare stable_key rows
```

This tells us which entities changed. Later, supplement this with explicit event hooks at gameplay operation sites.

Important identity rule:

- `stable_key` must not include mutable runtime state such as NPC position, waypoint, HP, amount, or mob state.
- Mutable fields must appear in the row body so diff reports them as `changed`.
- Inventory keys should identify an inventory slot/class relation, not a global row number, otherwise adding/removing one item shifts many keys.

The first save-diff test exposed this: NPC keys changed for every NPC because position/waypoint were part of identity, and inventory changes appeared mostly as added/removed because row order was part of identity.

The second test exposed another issue: `npcArr` is sorted during runtime, so `slot_id` is not stable either. The engine now assigns `Npc::persistentId()` when NPCs are inserted, saves it in save-game version `56`, and the exporter uses it for NPC identity.

The next test exposed the same class of issue for world items: `items.jsonl` used `slot_id` in `stable_key`, so picking one item could shift later array entries and create fake added/removed rows. Save-game version `57` now stores `Item::persistentId()` for world items, and exporter schema `3` uses it for item identity.

Schema `4` adds high-signal script state exports:

- `quests.jsonl`: quest/topic name, section, status, entries.
- `known_dialogs.jsonl`: NPC/info symbol pairs from `GameScript::dlgKnownInfos`.
- `script_globals.jsonl`: mutable Daedalus INT/FLOAT/STRING globals. This catches many script flags behind one-shot dialogs, repeatable dialogs, read books, rewards, bonuses, and other script-side state that is not visible in quest log rows.
- `npc_stats.jsonl`: per-NPC/player level, experience, learning points, attributes, protection, damage, hit chance, talents, mission slots, and aivar values. This catches the actual character-state effect of books, teachers, bonuses, combat rewards, and script-side stat changes.

This is meant to catch dialog with Xardas, readable objects, quest/log changes, and hidden Daedalus flags used by one-shot rewards or conditional dialog branches.

Current identity direction:

- NPC: `world + persistent_id + symbol_index + script_id`
- world item: `world + persistent_id + symbol_index + display_name`
- NPC inventory: owner persistent id + item persistent id + item symbol + equipped + slot
- mobsi: vob id + tag/focus/scheme/position
- mobsi inventory: owner vob id + item persistent id + item symbol + equipped + slot

Current delta classification rule:

- `summarize_world_events.ps1` is the preferred human report for gameplay deltas.
- Non-player inventory added for NPCs that did not otherwise change is treated as likely ambient script/init noise and hidden by default. Use `-ShowAmbientNpcInventory` to inspect it.
- Mobsi rows where only `state` changes from `-1` to a runtime value are treated as state initialization noise and hidden by default. Use `-ShowMobsiInit` to inspect them.
- Keep `compare_world_dumps.ps1 -Details` as the lower-level raw stable-key diff.
- From schema `5`, check `script_globals.jsonl` when a gameplay action is visible only through a script flag. Dialog and readable-object tests should inspect `known_dialogs.jsonl`, `quests.jsonl`, and `script_globals.jsonl` together.
- From schema `6`, check `npc_stats.jsonl` together with `script_globals.jsonl` for books/teachers/rewards: globals often explain why an effect fired once, while NPC stats show what changed on the character.
- `tools/compare_latest_world_dump.ps1` finds the newest `snapshots/tick_*` directory and runs both raw diff and event summary.
- `tools/export_world_events.ps1` converts two dump directories into normalized JSONL events for database staging. Default output hides init noise; `-IncludeAmbient` keeps ambient NPC inventory and mobsi init state.

Likely hook locations:

- item pickup/drop/move: `Inventory`, `Npc`, `World::takeItem`, `World::removeItem`
- food/plant/potion use: `Inventory::use`, `Npc::useItem`
- buy/sell/trade: `Npc::sellItem`, `Npc::buyItem`, inventory transfer helpers
- bow/crossbow: `Npc::shootBow`, `World::shootBullet`
- spells: `Npc::beginCastSpell`, `Npc::commitSpell`, `World::shootSpell`
- dialog: `GameSession::dialogExec`, `GameScript::exec`
- quest log: `GameScript::saveQuests`, `QuestLog`
- script globals: `GameScript::saveVar`
- mob/container/door: `Interactive::attach`, `Interactive::detach`, `Interactive::setMobState`

Short-term workflow:

1. Run with `-dump-initial-world exports`.
2. Also run with `-dump-save-world exports`.
3. Play and perform actions.
4. Make a normal save.
5. Compare initial dump with the save snapshot using `tools/compare_world_dumps.ps1`.
6. Generate a human event report using `tools/summarize_world_events.ps1`.
7. Classify each changed row as persistent DB state, transient runtime state, or append-only event.

Observed gameplay test:

- Talking to Xardas should appear in `known_dialogs.jsonl`, and possibly `quests.jsonl`.
- Reading the bookstand/pulpit can appear as `known_dialogs`, `quests`, script globals, or `mobsi.state` depending on the script.
- Pressing the wall button appears partially as a `mobsi.state` change around Xardas tower (`TOUCHPLATE`), but this is mixed with routine-state noise.
- Opening/looting the chest appears as `mobsi_inventory` removed rows and player inventory added rows.
- Picking/equipping the club appears as world item removed and player inventory added/equipped.
- Killing sheep/goblin appears as NPC `dead,hp`; spell use also changes player mana.

Useful commands:

```powershell
powershell -ExecutionPolicy Bypass -File tools\compare_world_dumps.ps1 `
  -Baseline exports\g2notr\newworld.zen `
  -Snapshot exports\g2notr\newworld.zen\snapshots\tick_<tick> `
  -Details

powershell -ExecutionPolicy Bypass -File tools\summarize_world_events.ps1 `
  -Baseline exports\g2notr\newworld.zen `
  -Snapshot exports\g2notr\newworld.zen\snapshots\tick_<tick> `
  -Limit 30
```
