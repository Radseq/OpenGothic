# API Contracts

## Client Flags

- `-mmo-client-server host:port` enables server-bound mode.
- `-mmo-server-endpoint host:port` is endpoint alias/config path.
- `-mmo-action-session-key KEY` selects the client/server session key.

Rules:

- No MMO/server behavior without explicit flags.
- Native single-player saves/new game must remain unchanged.

Stable C++ server flags:

- `--runtime-read-model PATH` / `--runtime-read-model-path PATH` - load
  `mmo.content_build_runtime_read_model.v1` during server startup.
- `--content-revision-key KEY` - active server content revision expected by the
  cache.
- `--world-instance-key KEY` - server world instance key bound to the cache.
- `--world-name NAME` - world name bound to the cache.
- `--startup-check-only` - validate startup/cache and exit before UDP loop.

Rules:

- Cache loading is read-only.
- Content revision mismatch is fatal by default.
- Cache loading does not execute scripts or mutate DB.

## Runtime Read-Model

Schema: `mmo.content_build_runtime_read_model.v1`.

Required sections:

- `world_zen_entities`
- `waypoint_edges`
- `npc_templates`
- `item_templates`
- `routines`
- `perception_bindings`
- `dialog_infos`
- `dialog_outputs`

Current expected warnings:

- routines without `npc_instance`: `1186`
- perception bindings without `owner_symbol`: `38`

Current C++ runtime indexes:

- `worldZenEntityByKey`
- `waypointEdgeByRoute`
- `npcTemplateByInstance`
- `itemTemplateByInstance`
- `routinesByNpcInstance`
- `routinesBySymbol`
- `perceptionBindingsByKind`
- `perceptionBindingsByOwner`
- `dialogInfoBySymbol`
- `dialogOutputByName`

## NPC Perception

Targets:

- `mmo_npc_perception_policy`
- `mmo_npc_perception_policy_probe`

Rules:

- Consumes `WorldInstanceContentCache` and explicit actor sets.
- Does not execute Daedalus scripts.
- Does not mutate DB or broadcast packets.
- Missing perception binding produces no decision.

## AI Runtime Recording

Targets:

- `mmo_npc_perception_runtime_source`
- `mmo_ai_runtime_persistence`
- `mmo_npc_perception_record_probe`

Rules:

- `--dry-run` must not mutate DB.
- Synthetic probe can use `--target-from-runtime-player` to copy target
  `session_uuid`/`character_uuid` from a real active player while keeping the
  NPC side synthetic.
- Recording must call `mmo_ai_record_npc_perception_decision(...)`.
- Required fields: `world_instance_uuid`, `npc_entity_key`, `target_key`,
  `perception_kind`, `idempotency_key`.
- Live actor-source rejects weak NPC identity by default.

## Manual World-Instance AI Tick

Targets:

- `mmo_world_instance_ai_tick`
- `mmo_world_instance_ai_tick_probe`
- `mmo_world_instance_ai_tick_evidence`
- `mmo_world_instance_ai_scheduler_boundary`

Rules:

- Default is dry-run.
- Write requires explicit `--write`.
- Evidence can reject missing actors, weak NPC identity, missing decisions or
  record-limit skips.
- `mmo_udp_server` startup hook is dry-run only and must not write decisions.

## AI Action Queue And Dispatcher

Targets:

- `mmo_npc_perception_action_queue_probe`
- `mmo_npc_perception_action_dispatcher_boundary`
- `mmo_npc_perception_action_dispatcher_probe`

Rules:

- Queue probe is read-only by default.
- Claiming requires explicit mutation flag.
- Dispatcher probe never calls `mmo_ai_mark_npc_perception_action_applied`.
- Live dispatch must report `false` until a future guarded send/apply step.

## Dialog Intent Preview Chain

Targets:

- `mmo_npc_perception_effect_descriptor`
- `mmo_npc_perception_dialog_intent_preview`
- `mmo_npc_perception_dialog_intent_diagnostic_packet`
- `mmo_npc_perception_dialog_intent_diagnostic_encoder`
- `mmo_npc_perception_dialog_intent_durable_evidence`
- `mmo_npc_perception_dialog_intent_fanout_plan`

Rules:

- Supported safe action kinds: `npc_greet_player`, `npc_warn_player`.
- Preview/diagnostic/encoding/evidence are contract/evidence only.
- JSONL evidence write requires explicit file-write flag.
- Fanout plan must not send packets.
- Fanout plan requires real target `session_uuid` and `character_uuid`.

## MySQL Surfaces

- runtime MMO DB: sessions, characters, current world state, journal/projections.
- `mmo_content_build`: parsed static content.
- `mmo_ai_runtime`: NPC perception decisions and action queue.

Rules:

- `mmo_content_build` is not live gameplay state.
- `mmo_ai_runtime` is runtime AI/perception decision state, not parser output.
- Procedure/table/view renames require migration/compat updates.

## Identity

Durable identity should use character key, world instance, content revision,
world name, persistent/VOB/script IDs, template keys and DB UUIDs.

Never use display labels as durable identity.
