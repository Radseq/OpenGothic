# 01 Persistence Stack

Native `.sav`:
- Must keep working. Used as compatibility and parity oracle.
- `GameSession::save` writes session clock, world, perception, quests, Daedalus vars and camera. `GameSession(Serialize&)` loads native save before optional DB restore.
- `WorldStateStorage` keeps per-world native snapshots under `worlds/<name>.zip`; use it for parity, not as MMO state.

Runtime SQLite:
- Owner: `game/game/mmoruntimesqlite.cpp`, created from `GameSession` only after world/script are ready.
- Flags in `game/commandline.cpp`: `-mmo-sqlite`, `-mmo-sqlite-interval-ms` clamped >=250ms, `-mmo-sqlite-no-restore`, `-mmo-sqlite-capture-baseline`.
- `runtime_*` = capture/diagnostics/history.
- `mmo_*_current` = physical restore projection.
- `mmo_world_baseline_*` = immutable New Game baseline.
- `mmo_save_slot_*` = per-native-slot durable snapshots. Loading a `.sav` restores DB state only from matching slot snapshot; missing snapshot keeps native `.sav` authority.
- Text must be durable UTF-8. CP1250/Latin fallback is diagnostic only.

MySQL production DB:
- Current target; uses InnoDB, `BINARY(16)` UUIDs with `UUID_TO_BIN(...,1)`, JSON, stored procedures, audit tables and explicit views.
- Core ownership: account, entitlement, content revision/templates, realm/world instances, characters, positions, stats, wallet, inventory, equipment, quests, dialogs, script state, world entities, item instances, world inventory/script, event journal.
- NPCs are currently represented as `world_entity_state.entity_kind='npc'|'creature'` plus lifecycle/audit/projection views. A dedicated hot-path NPC read projection can be added later, but it should not replace canonical `world_entity_state` yet.
- `world_event_journal` is append-only truth. Current-state tables are projections optimized for reads and server validation.
- Idempotency key uniqueness is mandatory per world instance. Repeated actions must not duplicate item/gold/XP/combat effects.

Implemented MySQL DB milestones:
- 001..014: production schema, import, login/checkpoint/logout, wallet, world item, inventory/equipment, container/interactive, quest/dialog/script/progression, NPC lifecycle, trade, combat/resource, item stack, diagnostics.
- 015..022: server action outbox, restore parity registry, event replay contract, readiness dashboard, dispatch contracts, worker telemetry, strict replay audit, parity artifacts.
- 023..030: final DB completion requirements, projection hashes, final integrity audit, DB restore manifest, backup/retention manifest, read models, external integration gates, final evaluator.

Current meaning of Step 30:
- `database_status='complete'`: DB contract is ready for server/hook/replay phase.
- `mmo_status='blocked'`: expected until external evidence exists.
