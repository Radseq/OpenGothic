# API Contracts

This file summarizes stable external/internal contracts that agents should not
break casually. Inspect source before changing exact packet or JSON fields.

## Client Flags

Known stable MMO opt-in flag:

```text
-mmo-client-server host:port
```

Rules:

- Without MMO flags, native single-player must behave unchanged.
- New MMO behavior should be gated behind explicit flags or server-bound mode.
- Avoid mandatory local-dev-only arguments in final UX. Development flags may exist, but the target flow is character selection/new character creation through the client UI.

## UDP Server Path

Current authority path:

```text
client hooks -> ASIO UDP binary packets -> server/cpp/mmo_udp_server -> direct C++/DB validation
```

Rules:

- Prefer typed binary/structured packets for hot gameplay.
- Keep sequence/duplicate validation where existing code expects it.
- ACK/NACK/diagnostic packets should be explicit and parseable.
- Do not route new authoritative gameplay through Python worker/outbox unless explicitly implementing fallback/debug support.

## Bootstrap Snapshot

Known server materialization contract:

```text
mmo_bootstrap_snapshot_v1
```

Rules:

- Snapshot is server-produced load-time materialization evidence.
- Snapshot may be chunked.
- Client writes received runtime/bootstrap artifacts under `runtime/`.
- Do not treat bootstrap JSON as the long-term live replication contract.

## Runtime Read-Model

Current generated artifact schema:

```text
mmo.content_build_runtime_read_model.v1
```

Rules:

- Content build/export owns the JSON artifact.
- C++ server loads the artifact into typed structs/indexes.
- Gameplay code should query typed indexes/cache, not repeatedly parse raw JSON.
- When schema changes, update exporter, loader, probe/checks and docs together.

Known Step226 index surfaces:

```text
world_zen_entity_by_key
waypoint_edge_by_route
npc_template_by_instance
item_template_by_instance
routine_by_npc_instance
routine_by_symbol
perception_binding_by_kind
perception_binding_by_owner
dialog_info_by_symbol
dialog_output_by_name
```

## Database Contracts

Current database roles:

- `mmo_content_build` - static parsed content/build-time schema.
- `gothic_mmo_ch1_clean` - runtime MMO state/projections/procedures.
- `mmo_ai_runtime` - AI/perception/runtime decision domain.

Rules:

- Content build tables are not live gameplay state.
- Runtime tables/procedures are server-authoritative development contracts.
- Prefer current projections and procedures for accepted state.
- Use schema-only dumps/check tools for agent context; do not dump live data into docs.

## Character Identity UX Target

Target direction:

- `Save` in MMO mode should be blocked or replaced by server-managed persistence.
- `New Game` should create or start a server-side character flow.
- `Load Game` in MMO mode should select server-side characters, not local save files.
- Final flow should not depend on hardcoded `PC_HERO`, `Ja`, local-dev session keys or manual CLI character keys.

Implementation must still preserve native single-player save/load outside MMO mode.
