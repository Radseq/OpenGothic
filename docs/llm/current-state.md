# Current State — Full OpenGothic Client

Last verified: 2026-07-14 against client source, focused CMake tests and graphical-launch integration review.

## Implemented MMO boundary

- `mmoclientbridge.cpp` consumes
  `src/client_sandbox/include/gothic/mmo/client_runtime_facade.h`;
- the bridge now owns a thin graphical-session coordinator over the facade for
  Protocol V2 hello/route binding, guest authentication, character roster,
  create/select, enter-world, heartbeat and resume after transport replacement;
- `ClientMmoSessionSnapshot` is the UI/GameSession-facing typed status model;
  the full client still owns no socket, codec, retry ledger or bootstrap
  assembler;
- client CMake adds the sandbox as a subdirectory and links the ASIO facade when
  available;
- endpoint/socket/worker/retry/bootstrap assembly are not implemented in the
  full-client bridge;
- `mmoclientadapter.*` exposes engine-facing domain requests for bootstrap,
  movement/checkpoints, interaction, inventory/equipment/loot/trade/consume,
  weapon state, combat and dialog choice; exact Protocol V2 item-stack handles,
  generations and expected inventory/equipment revisions are used by the
  full-client inventory UI;
- semantic hooks, adapter and dialog UI do not construct client wire packets;
  movement/interaction/combat/dialog requests map to typed Protocol V2 facade
  requests;
- adapter validation is fail-closed, checks numeric narrowing/text/finite values
  and structurally omits authoritative result fields such as character stats,
  wallet deltas and NPC dead/unconscious flags;
- JSON exists at a local diagnostic boundary and is not parsed back into wire
  gameplay packets.

## Session, bootstrap and menu

- New Game authenticates, creates/selects a server character and enters the
  authoritative world before constructing the graphical `GameSession`;
- Continue/Load obtains the typed server roster and selects by stable
  `CharacterId`; native `.sav` slots are not authoritative in MMO mode;
- Save is disabled in server-bound mode;
- `-nomenu` auto-entry keeps the menu visible when the network/session step
  fails instead of hiding the only recovery UI;
- completed typed bootstrap sections and server ACKs arrive through in-memory
  facade mailboxes; obsolete filesystem and legacy string/JSON bootstrap
  control paths are removed;
- completed bootstrap inventory/equipment sections retain their authoritative
  collection revisions and are installed into `ServerInventoryReadModel` and
  `ServerEquipmentReadModel`; the MMO inventory page never reads or mutates
  `Npc::inventory`;
- the server-backed inventory page exposes equip, unequip, use, drop, split and
  merge intents, marks exact stack handles pending after successful submission,
  performs no optimistic quantity/equipment mutation, and clears pending only
  after rejection or a newer relevant authoritative revision;
- `tools/run_mmo_graphical_client.py` starts the fixture-backed production UDP
  server and launches `Gothic2Notr` directly into the graphical MMO session.

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
  equipment slots, weapon modes, combat timelines/results, authoritative
  damage/HP, hit reactions, life state, dialog/busy state, interactives and
  movers, distinguishes reconciliation from teleport/resync hard-snap
  corrections, and requires exact entity generations;
- `ServerEntityPresentationRegistry` is the single owner of
  server-handle-to-local-NPC bindings;
- `ServerWorldObjectRegistry` separately owns the
  `WorldObjectId -> LocalVobToken -> EntityHandle` mapping for ZEN objects;
  local identities are rebuilt from the loaded VOB tree with the same stable
  key/hash contract as the authoritative importer, while runtime bindings,
  applied revisions and unresolved-log history are reset on route replacement;
- interactive and mover presentation resolves the local 32-bit `vobObjectID`
  through this registry, performs exact-generation replacement, skips stale,
  duplicate and unchanged states, and caps unresolved diagnostics per route;
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
- the main loop submits normalized movement axes, movement mode, input flags,
  predicted pose and last acknowledged server tick at a bounded cadence;
- player interaction resolves local NPC/interactive objects back to the exact
  server entity handle and revision before sending Talk/Loot/Use;
- weapon and melee input submits typed draw/holster/primary/secondary/parry
  commands with exact target handle/revision where available;
- every server-bound actor, including the local player, rejects local combat
  damage mutation, so predicted attack/hit animation cannot become client-side
  HP authority.
- the S6 presenter resolves equipped weapon visuals through the binary
  presentation catalog, binds melee/ranged meshes to back or hand attachment
  points, plays authoritative draw/holster, attack, parry, dodge, hit,
  knockback, unconscious and death presentation, and scopes camera shake to the
  local player;
- local combat prediction is presentation-only: an echoed predicted action is
  not replayed, rejected/cancelled actions interrupt and correct to the
  authoritative weapon mode, and only `DamageApplied`/life-state records write
  HP;
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
- bootstrap installation first binds stable world-object descriptors and logs
  aggregate mover bound/unresolved counts, then materializes the local player,
  dedicated remote-player/NPC replicas and installs NPC, interactive and mover
  baselines before live deltas;
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
- the optional bounded binary presentation-catalog runtime validates the
  admitted `ContentManifestId` and maps an exact
  `(ArchetypeId, PresentationId)` pair to a Daedalus instance name before NPC
  or remote-player materialization; the offline importer can publish the
  matching NPC `GMPCAT01` artifact and failed reloads clear the prior catalog;
- the direct script-symbol compatibility path remains intentionally bounded
  when no production catalog is configured and rejects hashed/catalog IDs it
  cannot resolve;
- the current typed dialog schema carries numeric line and choice revisions but
  not presentation text/audio or the choice list, so those UI elements remain
  blocked on a richer presentation/catalog contract;
- character create/select/load is functional for the initial graphical flow;
  richer account UX, character deletion/renaming and world-transition loading
  presentation remain open;
- character inventory/equipment/use-item UI now uses exact item-stack handles
  and revisioned server state; live `InventoryDelta` and
  `EquipmentSlotChanged` mailboxes update the read models, and item display
  names resolve through the admitted presentation catalog;
- typed world-item spawn/despawn/state events materialize revisioned local
  items, preserve exact entity handles for pickup intents and clear bindings on
  route reset; item icons, containers, trade and quest UI remain open;
- combat input and the protocol-independent S6 presentation consumer are wired
  for equipment, weapon mode, attack/parry/dodge, authoritative HP, hit
  reaction, knockback and death/unconscious state; facade/wire production of
  these typed records, projectile/spell presentation and richer effect-resource
  metadata remain incomplete;
- the two-client gate currently validates the production presentation domain
  boundary without launching ZenEngine/Vulkan; actual two-process rendering,
  animation, dialog UI and mover/door acceptance remains open;
- logical-session reconnect/resume is wired through the facade and bridge;
  durable recovery across a server-process restart still depends on the server
  durable-state configuration;
- SQLite capture/restore tooling under `src/client/tools/mmo` is optional and
  disabled by default; it is unrelated to the repository LLM search index.

## Authority rule

Native single-player continues to run original local logic. MMO-bound replicas
and server-backed UI consume server output; local animation/collision evidence
may support prediction or validation but never decides authoritative outcomes.
