# Full Client Current State

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
  command completion state without optimistic quantity mutation.
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
2. Normal inventory-key handling in server-bound mode currently refuses to open
   the inventory UI, despite typed read models and item commands existing.
3. Dialog presentation carries numeric session/line/revision state but not the
   complete user-facing subtitle, audio and choice payload needed for a native
   dialog experience.
4. Character-attribute and loot-availability facade records are valid but are
   intentionally not mapped because full-client presentation components do not
   yet exist for them.
5. Legacy semantic-hook and movement/NPC observation code still needs
   classification as native-only, diagnostic, intent adaptation or obsolete.
6. No focused test currently proves `mmoclientbridge`, menu orchestration or the
   full `GameSession` MMO projection against a fake facade.

Do not describe a feature as production-active merely because its DTO, mapper
or engine method exists.
