# 02 Domain Model

Identity:
- Character/player: `characters.character_key` such as `PC_HERO`, account/realm/world FK, lifecycle state.
- NPC/creature: world + persistent id + symbol index + script id. Do not use display name or runtime array index.
- World item: world + persistent id + symbol index/script/template. Item template symbol is not an item instance id.
- Interactive/container/door/mobsi: world + stable VOB id / interactive key.
- Item instance: DB item instance key + item template + owner type/id + quantity + lifecycle. Stack quantity is distinct from item identity.

Character state tables:
- `character_positions`: current authoritative position/checkpoint projection.
- `character_stats`: level, experience, experience_next, learning_points, hp/mana, strength/dexterity, guild/attitude, raw source stats.
- `character_wallets`: spendable currency. `g2notr:gold` is wallet-owned; source gold item rows are validation/compatibility only.
- `character_inventory`, `character_equipment`: source-faithful inventory rows and selected equipment projection.
- `character_quests`, `character_known_dialogs`, `character_script_state`: script-owned durable progress. Do not write VM internals directly; use approved restore/API paths.

World state tables:
- `world_entity_state`: canonical current projection for NPCs, creatures, loose items, interactives and other durable world entities.
- `world_inventory`: container/world entity inventory projection.
- `world_script_state`: durable world script vars.
- `item_instances`: global item instances by owner scope.
- Audit tables record accepted mutations but are not truth; event journal + projection is truth.

Important Gothic semantics:
- Inventory values are not interchangeable: `amount`, `iterator_count`, `equipped`, `equip_count`, `slot` must be preserved.
- Quest absence differs from obsolete quest. Quest statuses: Running=1, Success=2, Failed=3, Obsolete=4.
- Dialog phases differ: `dialog_choice_updated` is visibility/subchoice update; `dialog_choice_executed` is selected execution. Legacy `dialog_selected` is not production event type.
- Chapter progress is durable Daedalus INT global `KAPITEL`. `IntroduceChapter(...)` is presentation, not the durable source.
- Interactives: `stateId/stateCount/stateMask`, locked/cracked, container inventory are durable; animation timeline is not.
- NPC death/unconscious is not generic despawn. Death changes weapon state, AI/perception, physics, animation and script state.

Hot-path future projections to consider after server boundary:
- `world_npc_state`: dense NPC position/lifecycle/hp/mana/flags for server reads.
- `world_npc_ai_checkpoint`: stable target/state_other/state_victim/follow-escort intent only.
- `world_npc_trade_profile`: vendor inventory/prices/availability.
- These should be derived/projection tables from journal/current canonical state, not separate truth.
