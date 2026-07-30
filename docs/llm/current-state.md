# Full Client Current State

Last verified: 2026-07-30 against focused tests and a clean `Gothic2Notr`
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
- Typed inventory/equipment read models retain server revisions and pending
  command completion state without optimistic quantity mutation. The
  server-backed page model builds equip, unequip, use, drop, split and merge
  requests with exact handles and revisions, disables actions while pending or
  resynchronizing, and exposes accepted, rejected and resync-required feedback.
- Typed live dialog presentation stages bounded server choice labels, renders
  them in the native menu and submits the selected stable ID with the exact
  dialog revision; the UI waits for authoritative update/end instead of closing
  optimistically.
- Focused tests cover adapter validation, mailbox conversion, atomic bootstrap,
  route replacement, entity binding, interpolation, correction, catalog
  admission, inventory/equipment projection, combat presentation and typed
  projectile spawn/state/impact/despawn handling.
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
- Server replicas suppress local authority and use correction/interpolation
  paths.
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
3. Live dialog choices are selectable, but subtitle/audio still depend on a
   client-side numeric line lookup and bootstrap cannot restore choices for a
   dialog that was already awaiting input.
4. Character-attribute and loot-availability facade records are valid but are
   intentionally not mapped because full-client presentation components do not
   yet exist for them.
5. Legacy semantic-hook and movement/NPC observation code still needs
   classification as native-only, diagnostic, intent adaptation or obsolete.
6. No deterministic fake-facade test currently proves `mmoclientbridge`, menu
   orchestration or the full `GameSession` MMO projection; the present evidence
   is the headless page-model gate, sandbox regressions and full-client build.

Do not describe a feature as production-active merely because its DTO, mapper
or engine method exists.
