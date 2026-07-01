# 02 Gameplay Domains

Movement:
- Normal movement uses client proposal -> server authority gate -> accepted
  bounded checkpoint.
- Rejected movement must not mutate DB.
- Teleport/world change/chapter transition are separate semantic events and must
  not be smuggled through movement validation.

Bootstrap/materialization:
- At shard start, load hot read models into server memory: players, NPC spawns,
  stats, inventory/equipment, world items, interactives/containers, script/story
  state, world clock, waynet/routines.
- Materialization is state load, not thousands of semantic actions.
- Live actions start only after materialization and session join.

NPC AI/navigation:
- Waypoints/routines/navigation are server context; they are not yet
  authoritative AI.
- Distinguish routine/passive NPC from reactive proximity talk and hostile
  combat.
- Future `npc_dialog_initiated`/`npc_reaction_started` must be server validated
  by distance, perception, cooldown, dialog availability and NPC state.
- NPC-to-NPC actions need one server-owned transaction; do not let both NPCs
  independently mutate truth.
- Player-caused creature/NPC damage and death are now durable combat/lifecycle events, and dead/damaged lifecycle slices can be materialized on load. This is still projection persistence, not server-side combat or AI simulation.

Dialogs/quests/script:
- Quest status alone is insufficient. Persist status, entries/count, known
  dialogs, script globals and story progress.
- Dialog phases differ: visibility/update, selected execution, post-script
  effects.
- One-shot rewards such as bookstands/regals/Greg dig need durable flags and
  idempotent server events.

Inventory/equipment/items:
- Pickup/drop/loot/equip/unequip already have server-side bridge coverage.
- In server-bound mode, client restore from MySQL is selected by
  `-mmo-client-server`, not by per-domain flags. HERO stats/resources, inventory/equipment, position, quest log, known
  dialogs, safe full character script ints, world item tombstones, active server-owned world items and interactive/mobsi state can be applied from the server bootstrap snapshot, but this is still a
  load-time restore slice, not real-time replication.
- Containers/world item state must be current projection truth. Already-taken
  items stay gone after restart via `world_item_deltas` materialization. Active DB-owned world items, such as dropped items, can be spawned or updated from `active_world_items`. Container take uses a dedicated owner-aware hook; generic inventory transfer is not authoritative.
- Respawn must be explicit scheduled policy, never implicit baseline reload.

Interactive/mobsi:
- `use_interactive` records durable evidence.
- Persistent state changes like chest/door/lock/workbench/bookstand are restored from `interactive_state` for state id/locked/cracked when present. More complex per-mob semantics still need explicit server validation before real-time correction.

World time/sleep:
- Server-bound client sleep restores HP/mana locally and does not change local
  world time.
- Server-owned world clock needs a canonical transaction and routine
  recomputation policy.

Chapters/content overlays:
- Different chapters can add/remove NPCs/items. Model as gated content overlay,
  not world reset.
- Durable chapter source is script/story state (`KAPITEL` and related globals),
  not the chapter-intro UI.
- Future transaction should validate old/new chapter, activate/deactivate
  chapter-gated content, update story progress and journal once.

Testing priorities:
- First prove bootstrap and position/movement.
- Then dialogs/progression/quest/script-int.
- Then inventory/equipment/pickup/drop/loot roundtrip.
- Only then move toward live ACK consumption, server restore and replication.










Step80 gameplay note:
- Dialog/story restore is race-aware. A bootstrap snapshot may be older than a dialog the user just executed; in that case local known-dialog/quest mutations are preserved and script-int restore is skipped for that snapshot.




Step81 gameplay note:
- Server-bound bootstrap materializes two world slices safely: removed world items and interactive/mobsi state. It deliberately does not reposition NPCs or apply runtime AI/path state yet.




Step82 gameplay note:
- World item materialization now has two directions: non-active item tombstones remove baseline/save items when present, and `active_world_items` can spawn/update DB-owned world items. Container inventory is intentionally not applied through this path yet.



Step83 gameplay note:
- The first combat authority bridge is in place: player-caused NPC/world-entity damage, NPC death and dead-NPC loot can be journaled directly by the C++ server. NPC AI, aggro, perception, respawn and corpse cleanup policies remain future work.




Step84 gameplay note:
- Server-bound load now materializes NPC lifecycle state for dead/damaged NPCs and creatures so killed unique/world entities can remain dead after restart. Active NPC routine movement, wandering, eating, trading, dialogue initiation, perception and combat AI are not broadcast by the server yet.
- Unresolved local corpse/world pickup can be converted into a journaled character inventory grant by item symbol as a bridge until server-owned loot/drop spawning is authoritative.
