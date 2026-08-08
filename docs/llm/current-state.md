# Full Client Current State

Last verified: 2026-08-08 against focused tests and a clean `Gothic2Notr`
composition build.

This document separates four evidence levels. Source remains authoritative.

## Production-active composition

- The normal OpenGothic executable is C++23 and preserves native single-player.
- When the workspace sandbox target exists, the executable links its ASIO
  transport and enables the public runtime facade; a standalone build compiles
  with MMO transport disabled.
- SQLite capture/restore tooling is opt-in and disabled by default.
- Server-bound New Game and Continue/Load drive the facade session lifecycle:
  connect, authenticate or resume, obtain the roster, create/select a
  character, enter a world, then start the engine session.
- Save is not a client authority in server-bound mode.
- Runtime input and action paths translate movement, interaction, dialog,
  inventory and combat into typed intents. The bridge drains command
  completions and the typed presentation mailbox on the engine thread.
- In server-bound mode the normal inventory action opens a dedicated page over
  `ServerInventoryPresentationState`; native inventory behavior remains
  unchanged outside that mode.

## Implemented and focused-tested

- Native single-player telemetry is opt-in through
  `-native-telemetry <path.jsonl>` and is disabled when an MMO server flag is
  present. The parameter only arms the capture: `F12` starts it and a second
  `F12` stops it, with explicit lifecycle records in the JSONL file. While
  active, it writes one JSONL record per mouse press/release, game tick player
  position with yaw, and native world-item pickup attempt/success. Each record
  has an ordered sequence and a Unix millisecond timestamp. Pickup records
  retain player position, player yaw, source item coordinates and whether the
  native pickup succeeded. Movement-state and mob-distance feedback are not
  written, so this capture stays focused on the pickup distance and facing
  angle experiment. The generic JSONL writer remains reusable for a later
  telemetry event when another experiment needs it.
  The latest pickup run calibrated a 300-unit horizontal pickup range and a
  30-degree facing cone. The graphical client and sandbox reject an obviously
  invalid pickup before submitting it; the server repeats the distance check
  and, when it has a current movement-input yaw, the facing check.
  The latest native run calibrated production walk/run authority to 225/500
  units per second and bounds client/sandbox extrapolation to 500 horizontal
  units per second and 1450 vertical fall units per second. No native sneak
  sample was captured, so the existing sneak authority value remains unchanged.

- Engine intent DTO validation fails closed for incomplete legacy movement,
  generated interaction identity and missing item-stack revisions.
- A protocol-independent mailbox carries an optional route replacement, zero
  or more atomic bootstraps, then ordered live events.
- Facade output is validated and deterministically ordered by stream sequence
  before it reaches presentation state.
- Presentation state enforces route epoch, world generation, entity generation,
  baseline/revision monotonicity and bounded storage.
- Entity bindings prevent local-object aliasing; world-object bindings retain
  stable world-object identity separately from entity lifetime.
- Interpolation and movement correction reject stale, foreign-route and
  wrong-identity samples.
- In graphical server-bound mode the local player applies normal authoritative
  server transforms, not only explicit correction packets. Local Gothic
  movement cannot mutate its world position in this mode; rotation and visual
  input remain local, while position is owned by the server. This keeps the
  native Gothic position used for focus/pickup aligned with the server's
  fixed-tick position; the client-provided position remains diagnostic only.
- Typed inventory/equipment read models retain server revisions and pending
  command completion state without optimistic quantity mutation. The
  server-backed page model builds equip, unequip, use, drop, split and merge
  requests with exact handles and revisions, disables actions while pending or
  resynchronizing, and exposes accepted, rejected and resync-required feedback.
- The server-bound corpse page opens only from a replicated dead entity with
  authoritative loot availability. Its bounded read model constructs open,
  exact-stack, take-all and close requests with corpse, stack and inventory
  revisions; it waits for authoritative snapshot/delta/closed records, disables
  actions while pending and closes on reroute, despawn, decay or resync.
- Typed live dialog presentation stages bounded server choice labels, renders
  them in the native menu and submits the selected stable ID with the exact
  dialog revision; the UI waits for authoritative update/end instead of closing
  optimistically.
- Focused tests cover adapter validation, mailbox conversion, atomic bootstrap,
  route replacement, entity binding, interpolation, correction, catalog
  admission, inventory/equipment and corpse-loot projection, corpse-menu request
  adaptation, combat presentation and typed projectile
  spawn/state/impact/despawn handling.
- Projectile state is a self-contained upsert. A bounded full-client registry
  enforces frozen identity and route ownership, interpolates two authoritative
  samples, bounds extrapolation, pins impact positions and removes exact
  generations on despawn.

These tests compile selected full-client presentation components in the
sandbox test target. They do not instantiate the complete OpenGothic runtime.

## Composition-integrated but not fully proven

- `GameSession` consumes route, bootstrap and event records and materializes
  local player, remote player, NPC, world-object, interactive, mover, inventory,
  equipment and combat presentation into engine objects. It also owns sampled
  projectile presentation state, but actual mesh/particle/audio/decal objects are
  not materialized yet.
- Route replacement resets projection registries before installing the new
  bootstrap.
- An aggregate-revision rejection blocks further inventory submissions and
  requests a bounded Protocol V2 session restart; authoritative inventory and
  equipment replacement clears the resync requirement.
- The native corpse `ransack` path remains disabled in server-bound mode. The
  typed corpse page and production selective-loot snapshot/delta/closed
  projection are composition-integrated behind `TypedSelectiveLootV1`.
- Server replicas suppress local authority and use correction/interpolation
  paths.
- Server NPC replicas resolve the admitted presentation catalog's body, head,
  texture variants and captured default armor before they enter the world.
  Static MMO world objects
  also use their server transform as a bounded fallback when a transported
  world-object identity differs from the local VOB identity.
- World-object binding accepts the small coordinate difference caused by
  server quantization, so graphical interactive objects such as bookshelves
  can still resolve to their authoritative target. Replica NPCs clear local
  single-player weapon meshes before applying server equipment state, and
  startup world items use the same catalog/materialization path as dropped
  items.
- In graphical server mode an unmapped left mouse click is treated as the
  generic action. Book-like interactives submit authoritative `Read` and attach
  once to the native Gothic MOBSI state machine; there is no second hard-coded
  1.5-second timer or synthetic `onKeyInput` advance. A repeated action while
  already attached is consumed instead of restarting the presentation. This is
  still a graphical fallback: native MOBSI state functions can execute local
  script semantics, so the final server-bound cutover still requires explicit
  server-produced presentation events for script-affecting interactions. Item
  pickup submits the exact server item and inventory revisions and starts only
  the native pickup animation locally; world/inventory mutation remains server
  owned. A pending pickup blocks repeated mouse-down submissions for the same
  server item and is released by its receipt, despawn or transport timeout.
  When a real native world VOB had to provide an item presentation that
  was absent from the transported catalog, the client retains that exact
  instance/name binding for the resulting server-backed inventory stack.
- Reused native Gothic NPCs retain a bounded presentation-only vertical offset
  against the authoritative transform, avoiding visible floating caused by a
  different authored spawn anchor. During an authoritative dialog activity the
  replica also faces its replicated target after each transform sample; neither
  adjustment changes server position/rotation authority.
- Main-menu orchestration, the full-client reconnect/resume path, bridge
  locking and complete engine materializers are not directly covered by the
  focused unit target. Their strongest evidence is build/process/graphical
  execution.

## Known gaps and misleading legacy surfaces

1. The old string-based restore subsystem is still compiled and polled, but the
   bridge returns no legacy bootstrap snapshots and never populates its stored
   legacy snapshot. Typed mailbox bootstrap is therefore the effective path;
   the unreachable compatibility path is removal debt, not a supported mode.
2. The server-backed inventory page has focused model coverage and a complete
   client build, but still needs a graphical server-bound smoke covering visible
   pending/rejection/resync feedback and native-mode non-regression.
3. Live dialog choices are selectable and Protocol V2 now carries bounded
   authoritative UTF-8 line text. The full client deliberately does not
   reinterpret the stable numeric line ID as a Gothic OU/message key because
   it can resolve the wrong subtitle. Bootstrap restoration of the current
   line text and choices remains missing; ordinary eager startup NPCs still
   use the server's static fallback dialog until the real Daedalus dialog
   executor is production-composed.
4. Character-attribute facade records still have no full-client presentation
   component. Loot availability and corpse session records are mapped and
   emitted by production composition, but real graphical acceptance remains.
5. Legacy semantic-hook and movement/NPC observation code still needs
   classification as native-only, diagnostic, intent adaptation or obsolete.
6. No deterministic fake-facade test currently proves `mmoclientbridge`, menu
   orchestration or the full `GameSession` MMO projection; the present evidence
   is the headless page-model gate, sandbox regressions and full-client build.
7. The graphical launcher requires an explicit authority source. Real Gothic
   II / Night of the Raven validation uses `--content-root`; `--fixture` is an
   explicit synthetic process-gate mode and is not equivalent to the native
   rendered world.

Do not describe a feature as production-active merely because its DTO, mapper
or engine method exists.
