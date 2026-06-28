# MySQL NPC Lifecycle Write Path

Migration `009_npc_lifecycle_write_path.sql` adds the first server-owned NPC lifecycle mutations.

## New table

`world_npc_lifecycle_audit` records accepted NPC lifecycle events after the event journal append and
after the `world_entity_state` projection update.

## Procedures

`mmo_mark_npc_dead(...)` validates an active session, finds an active NPC/creature in the current
session world, appends `npc_marked_dead`, updates `world_entity_state.lifecycle_state` to `dead`,
sets `health_current` to `0`, increments `row_version`, and writes audit metadata.

`mmo_respawn_npc(...)` validates an active session, finds a non-active NPC/creature in the current
session world, appends `npc_respawned`, updates lifecycle, transform, health, state JSON and
`row_version`, then writes audit metadata.

## Deliberate scope

This is not full combat yet. It persists durable consequences of combat/death/respawn. Animation state,
AI queues, target heuristics and transient fight controller state stay outside the production DB model.
