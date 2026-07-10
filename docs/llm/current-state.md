# Current State

Last known project context from uploaded/provided files: Step226 / 2026-07-07.

If this file conflicts with current source code, inspect the source code first.

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

The old Python receiver/worker/outbox path is debug/fallback debt. New gameplay
work should target the C++ server path.

## Current Content Loop

```text
Gothic install/content root/VDF
-> C++ ZenKit content-build importer
-> parser_snapshot.json
-> mmo_content_build
-> runtime read-model JSON
-> C++ runtime read-model indexes
-> future server world_instance cache
```

Current content status:

- VDF world ZEN extraction works for `newworld.zen`.
- Latest known import produced:
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
- Step225 C++ `mmo_runtime_read_model_probe` validates counts/schema/hash and returns `status=ready`.
- Step226 materializes read-only C++ runtime indexes and reports index counts plus deterministic lookup checks.

Known Step226 index counts:

```text
world_zen_entity_by_key=24917
waypoint_edge_by_route=3202
npc_template_by_instance=730
item_template_by_instance=835
routine_by_npc_instance=0
routine_by_symbol=1186
perception_binding_by_kind=6
perception_binding_by_owner=0
dialog_info_by_symbol=4139
dialog_output_by_name=20826
```

Expected Step226 warnings:

```text
routines without npc_instance=1186
perception bindings without owner_symbol=38
```

## Stable Facts

- `-mmo-client-server host:port` opts the client into server-bound behavior.
- Without MMO flags, native saves/new game remain unchanged.
- Server-bound materialization is load-time restore/bootstrap, not live replication.
- The server sends bootstrap ACK and chunked `mmo_bootstrap_snapshot_v1`.
- The client writes bootstrap files under `runtime/`.
- Shared authority policy now treats client-side animation/audio/UI as presentation only and movement as prediction reconciled by server snapshots.
- Client content manifest hashes are evidence for server validation, not gameplay authority.
- The old local `mmonetprotocol.h` and `mmosemanticevents.*` shims have been removed; full-client MMO hooks include shared protocol headers directly.
- `src/client/server/` has been removed; server-owned C++ tooling now lives under `src/server/cpp`.
- NPC lifecycle/death persistence exists.
- Live NPC movement/AI is not yet server-simulated.

## Main Gaps

- Runtime read-model is indexed in C++, but not yet bound to a server `world_instance` cache.
- Live NPC movement/pathing/routine simulation is not implemented.
- Server script/perception/dialog tick is not implemented.
- Trade/spell/resource edge cases are not fully moved to direct C++ handlers.
- DB schemas are production-shaped development contracts, not final MMO storage.

## Current Next Edge

Build the first read-only server `world_instance` content cache on top of the
Step226 `RuntimeReadModel` indexes.
