# 10 Step46 Consumables / World Clock / AI Context

Step46 addresses the next live holes found after Step45.

Observed issues from real gameplay:
- the player looted meat from a killed wolf and ate it to regenerate HP, but the live server did not show an item-use/consumption row;
- NPC holstering could spam `holster_weapon` rows because AI kept calling the same weapon-state transition;
- sleeping until the next morning changed the world clock without a semantic server event;
- waypoint/routine data and NPC-to-NPC conversation need separation between durable MMO state and transient presentation.

C++ changes:
- `Inventory::use(...)` snapshots item count and item persistent id before script `on_state`, then emits `consume_item` only if the item count actually decreased after the use succeeds. This covers food/meat consumed by scripts, not just torch deletion.
- `Npc::changeAttribute(...)` positive HP/mana deltas for the player emit `character_resource_delta` capture rows. This makes food healing visible at the server boundary.
- `World::setDayTime(...)` emits `world_time_changed` after the time skip and before routine reset. This covers bed/sleep operations such as sleeping to morning.
- weapon-state capture uses a fixed-size semantic cache keyed by actor persistent id + symbol + player flag. Repeated calls that request the same already-emitted final state are suppressed before they reach UDP/outbox.

Server/worker/tool changes:
- `server/mmo/actions.py` recognizes `character_resource_delta` and `world_time_changed`.
- `run_mmo_resolved_action_worker.py` treats those two as capture-only applied no-op until canonical MySQL procedures are added.
- `check_mmo_step46_consumables_sleep_ai_context.py` reports resource, world-clock, weapon, corpse-loot and world-AI coverage in one pass.

Production interpretation:
- `consume_item` is a real mutation and should use the existing inventory consumption path.
- `character_resource_delta` should become a real DB procedure later, for positive HP/mana changes not represented by damage/mana consumption.
- `world_time_changed` should become a real world-clock procedure later, probably updating world-instance clock projection and journaling the sleep/rest/time-skip reason.
- Waypoints, waypoint edges, NPC routines and navigation checkpoints already belong to runtime/restore/read-model state. They are server-side context for validation/AI, not high-frequency live semantic events.
- NPC-to-NPC speech/audio lines are mostly transient. Do not persist every line. Persist only durable outcomes: relation checkpoint changes, quest/dialog/script changes, combat/death, inventory/resource deltas, and world-clock/routine changes.
