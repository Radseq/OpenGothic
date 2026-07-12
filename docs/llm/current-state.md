# Current State — Full OpenGothic Client

Last verified: 2026-07-12 against client source and focused CMake tests.

## Implemented MMO boundary

- `mmoclientbridge.cpp` consumes
  `src/client_sandbox/include/gothic/mmo/client_runtime_facade.h`;
- client CMake adds the sandbox as a subdirectory and links the ASIO facade when
  available;
- endpoint/socket/worker/retry/bootstrap assembly are not implemented in the
  full-client bridge;
- `mmoclientadapter.*` exposes engine-facing domain requests for bootstrap,
  movement/checkpoints, interaction, inventory/equipment/loot/trade/consume,
  weapon state, combat and dialog choice;
- semantic hooks and dialog UI do not construct client wire packets; only the
  private adapter mapping and bridge compatibility boundary know the historical
  packet variants;
- adapter validation is fail-closed, checks numeric narrowing/text/finite values
  and structurally omits authoritative result fields such as character stats,
  wallet deltas and NPC dead/unconscious flags;
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

- `ServerEntityPresentationRegistry` is the single owner of
  server-handle-to-local-NPC bindings;
- bindings are checked by entity generation, local world generation, world
  instance, entity kind and stable identity;
- the registry is bounded and prevents two server handles from aliasing one
  local object;
- despawn/invalidation is exact-handle and generation safe; generation changes
  release the old local binding and require an explicit rebind;
- local player, remote player and NPC presentation kinds are distinct;
- interpolation is route-scoped, bounded, rejects stale/foreign/local-player
  samples and reuses the frame output buffer;
- a movement-correction boundary owns pending correction data and validates the
  bound local-player handle, route and server tick before later application;
- replicated non-local NPC objects are marked `mmoServerReplica`, disabling
  local routine, perception, regeneration and combat authority while retaining
  presentation;
- server dialog revisions/choices drive presentation; client sends only a
  domain choice request.

## Transitional debt

- `mmosemantichooks.*` still exposes many observation/result-shaped callbacks
  inherited from migration history; diagnostic-only hooks need continued
  classification/reduction;
- the bridge/facade still exposes compatibility packet types internally; facade
  V2 should replace those mappings without changing engine call sites;
- current entity transforms are translated from the legacy delta packet at the
  presentation boundary; authoritative Protocol V2 route epochs and typed S2C
  lifecycle/correction events are not connected yet;
- complete character create/select/load UX, typed inventory/combat/quest UI and
  world-transition flow are not finished;
- MMO save replacement and reconnect recovery are not complete;
- SQLite capture/restore tooling under `src/client/tools/mmo` is optional and
  disabled by default; it is unrelated to the repository LLM search index.

## Authority rule

Native single-player continues to run original local logic. MMO-bound replicas
and server-backed UI consume server output; local animation/collision evidence
may support prediction or validation but never decides authoritative outcomes.
