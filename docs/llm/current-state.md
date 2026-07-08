# Current State

Last verified context: Step230 / 2026-07-07.

If this file conflicts with current source code, inspect the code first.

## Active Goal

Turn OpenGothic/Gothic II NotR into a server-authoritative MMO while preserving
native single-player behavior unless explicit MMO flags are used.

## Current Authority Loop

```text
OpenGothic client
-> ASIO UDP binary packets
-> C++ MMO server
-> MySQL procedures/read models
-> journal/current projections
-> bootstrap snapshot or ACK/NACK/diagnostic packets
```

The old Python receiver/worker path is debug/fallback debt. New gameplay work
should target the C++ server path.

## Current Content Loop

```text
Gothic install/content root/VDF
-> C++ ZenKit content-build importer
-> parser_snapshot.json
-> mmo_content_build
-> runtime read-model JSON
-> C++ runtime read-model indexes
-> C++ world_instance content cache
-> C++ server startup/session cache integration
-> C++ NPC/perception candidate assessment
-> C++ mmo_ai_runtime decision recording probe
-> future scheduled world_instance AI tick
```

Current content status:

- VDF world ZEN extraction works for `newworld.zen`.
- Latest import produced:
  - `zen_entities=24917`
  - `waypoint_edges=3202`
  - `daedalus_symbols=74651`
  - `npc_templates=730`
  - `item_templates=835`
  - `routines=1186`
  - `perception_bindings=38`
  - `dialog_infos=4139`
  - `dialog_outputs=20826`
- Step224 exports `mmo.content_build_runtime_read_model.v1`.
- Step225 C++ `mmo_runtime_read_model_probe` validates the exported read-model
  counts/schema/hash and returns `status=ready`.
- Step226 materializes read-only C++ runtime indexes and the probe reports
  `index_counts` plus deterministic `lookup_checks`.
- Step227 adds read-only C++ `WorldInstanceContentCache` and
  `mmo_world_instance_content_cache_probe`.
- Step228 wires `WorldInstanceContentCache` into `mmo_udp_server` startup behind
  explicit server flags and adds `--startup-check-only`.
- Step229 adds C++ `mmo_npc_perception_policy` and
  `mmo_npc_perception_policy_probe` for deterministic candidate decisions.
- Step230 adds C++ runtime actor-source, AI runtime persistence adapter and
  `mmo_npc_perception_record_probe` for explicit recording through
  `mmo_ai_record_npc_perception_decision(...)`.
- Current Step226 index counts:
  - `world_zen_entity_by_key=24917`
  - `waypoint_edge_by_route=3202`
  - `npc_template_by_instance=730`
  - `item_template_by_instance=835`
  - `routine_by_npc_instance=0`
  - `routine_by_symbol=1186`
  - `perception_binding_by_kind=6`
  - `perception_binding_by_owner=0`
  - `dialog_info_by_symbol=4139`
  - `dialog_output_by_name=20826`
- Current expected Step226 warnings:
  - routines without `npc_instance`: `1186`
  - perception bindings without `owner_symbol`: `38`
- Current Step227 cache stats for `world_instance_key=newworld`,
  `world_name=newworld`, `content_revision_key=gothic2-notr-steam-local`:
  - `world_zen_entities_in_world=24917`
  - `waypoint_edges_in_world=3202`
  - `npc_templates=730`
  - `item_templates=835`
  - `routines=1186`
  - `perception_bindings=38`
  - `dialog_infos=4139`
  - `dialog_outputs=20826`

## Stable Facts

- `-mmo-client-server host:port` opts the client into server-bound behavior.
- Without MMO flags, native saves/new game remain unchanged.
- Server-bound materialization is load-time restore, not live replication.
- The server sends bootstrap ACK and chunked `mmo_bootstrap_snapshot_v1`.
- Client writes bootstrap files under `runtime/`.
- NPC lifecycle/death persistence exists, but live NPC movement/AI is not yet
  server-simulated.

## Main Gaps

- Runtime read-model is indexed, bound to a read-only C++ `world_instance`
  content cache and loadable during C++ server startup/session bootstrap.
- Active read-model selection is still explicit via server flags, not yet
  DB-selected from `mmo_content_build.content_build_runtime_exports`.
- Live NPC movement/pathing/routine simulation is not implemented.
- Server NPC/perception candidate assessment can read live runtime DB actors and
  record explicit decisions to `mmo_ai_runtime` through a probe, but this is not
  yet scheduled by the UDP server/world_instance tick.
- Current live DB sample exposes weak NPC identity (`creature:None`, empty
  `npc_instance`), so production live recording should first fix content-backed
  NPC identity.
- Server script/dialog tick is not implemented.
- Trade/spell/resource edge cases are not fully moved to direct C++ handlers.
- DB schemas are production-shaped development contracts, not final MMO storage.

## Current Next Edge

Fix runtime NPC identity for live actors, then add an explicit scheduled
world_instance AI tick that invokes the Step230 writer.
