# Current State — Full OpenGothic Client

Last verified: 2026-07-13 against client source and focused CMake tests.

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
- semantic hooks, adapter and dialog UI do not construct client wire packets;
  movement/interaction/combat/dialog requests map to typed Protocol V2 facade
  requests;
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

- `mmoserverpresentationevents.h` defines protocol-independent, typed
  full-client DTOs for route/world descriptors, bootstrap roster/baselines and all live
  presentation families; it has no JSON, ASIO or sandbox implementation
  dependency;
- `ServerPresentationState` validates connection/route epoch and server world
  id/generation, installs bootstrap state atomically, exposes the baseline only
  after entity/NPC/interactive/mover validation, and clears corrections/dialogs
  on route replacement;
- typed state application stores revisioned transforms, NPC logical state,
  dialog/busy state, interactives and movers, distinguishes reconciliation from
  teleport/resync hard-snap corrections, and requires exact entity generations;
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
- unknown legacy remote-player/NPC transform identities are now materialized as
  dedicated MMO-owned presentation proxies instead of remaining unresolved;
- local bindings carry a stable object token plus explicit MMO ownership, so
  vector compaction cannot alias bindings and exact despawn removes only an
  MMO-created object; pre-existing world NPCs are merely detached from replica
  mode;
- replicated non-local NPC objects are created without local
  `RefreshAtInsert`, remain in `AiFar2`, and reject local routine, perception,
  regeneration and combat authority while retaining animation/presentation;
- server dialog revisions/choices drive presentation; client sends only a
  domain choice request.
- the deterministic two-client Protocol V2 gate builds two independent typed
  facade mailbox cuts and applies them through
  `mapClientRuntimePresentationMailbox` plus `consumeServerPresentationBatch`
  into separate `ServerPresentationState` instances, including bootstrap,
  live deltas, corrections, despawn, reconnect reset and world transition;

## Transitional debt

- `mmosemantichooks.*` still exposes many observation/result-shaped callbacks
  inherited from migration history; diagnostic-only hooks need continued
  classification/reduction;
- the bridge/facade compatibility packet submission boundary is removed;
  transform-only checkpoint movement, one-shot menu bootstrap, legacy inventory
  IDs and string-only combat/dialog targets now fail closed until their engine
  DTOs carry exact V2 identities and revisions;
- `mmoserverpresentationfacadeadapter.h` now performs the thin, one-way
  facade-domain mapping into `ServerPresentationBootstrap` and
  `ServerPresentationEvent`; separate facade mailboxes are merged by
  `streamSequence`, invalid records are rejected fail-closed and the bridge
  exposes one typed presentation batch without wire structs;
- `GameSession` drains one typed presentation mailbox cut, applies route,
  bootstrap and live records through `ServerPresentationState`, and invokes
  engine presenters only after typed validation succeeds;
- route replacement and world changes release exact entity bindings, clear
  interpolation/correction state and close active typed dialog UI;
- bootstrap installation materializes the local player first, then dedicated
  remote-player/NPC replicas, applies authoritative transforms and installs NPC,
  interactive and mover baselines before live deltas;
- live transform deltas use the route-scoped interpolator, local movement
  correction distinguishes reconciliation from teleport/resync hard snaps, and
  exact generation replacement/despawn releases only the matching binding;
- typed NPC state drives coarse idle/traversal and life-state presentation on
  server replicas; dead/unconscious projection no longer invokes local
  persistence/gameplay death hooks, preventing authority feedback loops;
- typed dialog start/update/end/busy events now reach `DialogMenu`; unresolved
  line IDs and absent choice payloads fail closed instead of being converted
  back into legacy dialog wire packets;
- the old entity-transform and dialog-presentation mailbox drains were removed
  from the full-client bridge;
- production `PresentationId`/`ArchetypeId` values still require a client
  resource catalog; the direct script-symbol compatibility path is intentionally
  bounded and rejects hashed/catalog IDs it cannot resolve;
- the current typed dialog schema carries numeric line and choice revisions but
  not presentation text/audio or the choice list, so those UI elements remain
  blocked on a richer presentation/catalog contract;
- complete character create/select/load UX, typed inventory/combat/quest UI and
  world-transition flow are not finished;
- F3 instrumentation is integrated with the public sandbox facade and records
  route/bootstrap installation, exact local/remote/NPC materialization,
  correction, dialog, interactive, mover and presented Vulkan frames. The
  process runner can launch two graphical clients and restart the server, but
  the acceptance run still requires a complete dependency checkout, `glslangValidator` and private Gothic
  assets;
- MMO save replacement and reconnect recovery are not complete;
- SQLite capture/restore tooling under `src/client/tools/mmo` is optional and
  disabled by default; it is unrelated to the repository LLM search index.

## Authority rule

Native single-player continues to run original local logic. MMO-bound replicas
and server-backed UI consume server output; local animation/collision evidence
may support prediction or validation but never decides authoritative outcomes.
