# Step126 - MMO server authority roadmap and engineering rules

Purpose: preserve the current architectural decision before starting the real
MMO server work. The client is being reduced to renderer, input, UI,
prediction, interpolation and snapshot/delta materialization. The server must
become the gameplay authority.

Durable engineering rules:
- Use C++23.
- Prefer `constexpr` for constants and pure compile-time helpers when it
  improves correctness or performance without making the code obscure.
- Runtime performance is a first-class goal. Treat server tick, validation,
  replication and DB-bound write paths as hot or latency-sensitive until proven
  otherwise.
- Keep code safe, explicit, readable and production-shaped.
- Favor small functions, narrow modules and separation of concerns.
- Design server domains so one implementation can later be replaced without
  rewriting unrelated systems.
- Preserve native single-player behavior unless an explicit MMO/server mode is
  active.
- Treat the current database as a temporary bridge. It is allowed to support
  development authority and restore parity, but it is not the final MMO
  persistence design and must be isolated behind replaceable server modules.

Authority boundary:
- Client keeps camera, audio, particles, render state, focus/highlight, UI,
  local prediction, animation pose and snapshot materialization.
- Server owns gameplay decisions, validation, accepted mutations, world time,
  routine scheduling, movement authority, item ownership, combat resolution,
  dialog/quest/script effects, interactive/mobsi effects, triggers, movers,
  spawn/despawn and replication deltas.
- DB stores durable facts, event journal, current projections and checkpoints.
  It is not a dump of transient client engine state.

Do not promote these client internals into production MMO truth:
- raw pointers;
- raw `AiQueue`, fight queues or move algorithm internals;
- animation frame/pose;
- audio, render, camera, particles, focus or menu state;
- local physics internals beyond data needed for server validation;
- debug SQLite captures.

Primary gameplay logic to move server-side:
1. World clock, sleep/rest effects, delayed timers and routine scheduling.
2. NPC AI, routines, waypoint/freepoint resolution and pathing.
3. Perception, detection, aggro, crime reaction and target selection.
4. Movement validation, authoritative correction and reconciliation.
5. Inventory, equipment, containers, trade and item ownership.
6. Combat, spells, damage, death, XP and loot.
7. Dialog, quest, script-state mutations and one-shot rewards.
8. Interactives/mobsi, bookstands, locks, doors, beds and workbenches.
9. Triggers, movers, delayed trigger queues and mechanism state.
10. World item lifecycle, respawn, spawn/despawn and interest streaming.
11. Chapter overlays, teleport and world transitions.

Near-term priority:
- Close existing authority gaps before adding large AI systems:
  client rollback/correction after NACK, movement correction,
  inventory/equipment ACK deltas, container authority, interactive NACK
  rollback and combat/resource correction.
- Replace JSON snapshot hot paths with typed live deltas:
  `ItemSpawnDelta`, `ItemRemoveDelta`, `NpcTransformDelta`, `NpcStateDelta`,
  `InteractiveStateDelta`, `MoverStateDelta`, `CharacterStatsDelta`,
  `InventoryDelta` and `QuestDialogDelta`.
- Continue the Xardas/DB Continue line by making server-owned world clock,
  routine bootstrap, current waypoint/target waypoint and NPC transform/routine
  deltas reliable before attempting full combat AI.

Suggested server module direction:
- `world_clock`: authoritative time, sleep/rest, scheduled world timers.
- `entity_identity`: stable IDs for characters, NPCs, VOBs, items and worlds.
- `movement_authority`: proposal validation, correction generation and
  reconciliation metadata.
- `inventory_authority`: owner-aware item transactions and equipment rules.
- `npc_runtime`: routine, navigation, perception and combat state in RAM.
- `routine_engine`: time-based TA/routine resolution without raw client queues.
- `navigation`: waypoint/freepoint graph loading and path following.
- `interactive_authority`: mobsi/container/door/bookstand validation and
  effects.
- `trigger_scheduler`: delayed trigger queue and mover state changes.
- `combat_authority`: hit validation, resource consumption, damage, death, XP
  and loot.
- `replication`: interest windows and typed deltas.
- `persistence`: event journal, projections, checkpoints and idempotency.
  The current DB implementation is intentionally bridge-shaped and should be
  replaceable when the production schema/write model is redesigned.

Important design principle:
- If a piece of data is needed only to continue an animation or local feel, it
  belongs on the client.
- If it decides who owns an item, whether an NPC reacts, whether damage happens,
  whether a script flag changes, or whether the world state mutates, it belongs
  on the server and must have a durable event/projection path.

Immediate next implementation candidates:
1. Add a small C++ server module skeleton for authoritative world tick and
   domain services, keeping the current UDP server entry point thin.
2. Introduce typed movement correction and inventory/equipment delta packets on
   the live path, using existing DB correction/projection rows as bridge data.
3. Add server-owned NPC routine target reconstruction from world time plus
   waypoint/freepoint identity, without persisting raw `AiQueue`.
4. Start extracting validation code into narrow services so future server ticks
   do not grow inside `mmo_udp_server.cpp`.
