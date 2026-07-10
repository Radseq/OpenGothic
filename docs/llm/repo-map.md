# Repository Map

Use this as a navigation aid only. Verify paths in the actual repo before editing.

## Top-Level Areas

| Path | Purpose |
| --- | --- |
| `game/` | OpenGothic client/runtime source. Preserve native single-player behavior. |
| `server/cpp/` | C++ MMO server, UDP path, content tools/probes/loaders. |
| `server/sql/` | MySQL schemas/procedures/migrations/development SQL. |
| `tools/` | Python/shell tooling for DB, content build, snapshots and checks. |
| `docs/llm/` | Compact context for AI agents. |
| `docs/llm/ai/` | Historical step archive. Do not load by default. |
| `runtime/` | Generated local outputs, parser snapshots, bootstrap artifacts, check outputs. |
| `build/` | Generated build tree. Do not commit/edit manually. |

## Server C++ Areas

Likely files/modules to inspect for current work:

- `server/cpp/mmo_udp_server.cpp` - main UDP server/authority path.
- `server/cpp/mmo_runtime_read_model_loader.*` - runtime read-model loading/indexing.
- `server/cpp/*read_model*probe*` - focused validation/probe tooling.
- `server/cpp/*content_build*` - content build importer/exporter/probes.
- `server/cpp/CMakeLists.txt` - server-side targets.

For Step226 -> next step, inspect loader/index structs first, then add a
read-only `world_instance` cache around them.

## Client Areas

Client changes must be conservative:

- preserve default Gothic behavior;
- isolate MMO mode behind explicit flags;
- emit server intents/evidence rather than authoritative mutations;
- avoid hard dependencies on local DB/server when not in MMO mode.

Likely search terms:

```text
mmo-client-server
mmo_action
bootstrap_snapshot
server_bound
save/load
PC_HERO
```

## SQL/DB Areas

When schema/procedure changes are required:

- update SQL under `server/sql/`;
- update apply/check tools under `tools/`;
- update `testing.md` with the smallest verification command;
- document durable contract changes in `api-contracts.md` or `decisions.md`.

Do not add views/procedures just to hide unclear application logic. Prefer typed
server logic unless the DB contract clearly owns the operation.

## Tooling Areas

Snapshot/context tools should:

- split server and client code snapshots when possible;
- include `docs/llm` and relevant tools;
- avoid `build/`, `.git/`, binaries, images, logs and generated snapshots;
- export MySQL schema without live data;
- skip broken MySQL views when needed and report that explicitly.
