# Current State — Full OpenGothic Client

Last verified: 2026-07-11 against client source and CMake.

## Implemented MMO boundary

- `mmoclientbridge.cpp` consumes
  `src/client_sandbox/include/gothic/mmo/client_runtime_facade.h`;
- client CMake adds the sandbox as a subdirectory and links the ASIO facade when
  available;
- endpoint/socket/worker/retry/bootstrap assembly are not implemented in the
  full-client bridge;
- client submissions use the shared client-intent variant;
- JSON exists at a local diagnostic boundary and is not parsed back into wire
  gameplay packets.

## Bootstrap and menu

- completed snapshot payloads and server ACKs arrive through in-memory facade
  mailboxes;
- menu bootstrap rejection/status reads the in-memory bridge;
- obsolete filesystem ACK/rejection control paths are removed;
- the snapshot body remains the historical in-memory string/JSON schema until
  typed binary bootstrap sections replace it.

## Presentation

- server entity transform deltas map stable server IDs/generations to local
  objects and use interpolation;
- replicated NPCs are marked `mmoServerReplica`, disabling local routine,
  perception, regeneration and combat authority while retaining presentation;
- server dialog revisions/choices drive presentation; client sends only a
  choice request.

## Transitional debt

- `mmosemantichooks.*` still exposes many observation/result-shaped callbacks
  inherited from migration history; production submission must remain intent
  only and these hooks need classification/reduction;
- the facade still exposes compatibility packet types rather than a final
  domain-level API;
- complete character create/select/load UX, typed inventory/combat/quest UI and
  world-transition flow are not finished;
- MMO save replacement and reconnect recovery are not complete;
- SQLite capture/restore tooling under `src/client/tools/mmo` is optional and
  disabled by default; it is unrelated to the repository LLM search index.

## Authority rule

Native single-player continues to run original local logic. MMO-bound replicas
and server-backed UI consume server output; local animation/collision evidence
may support prediction or validation but never decides authoritative outcomes.
