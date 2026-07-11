# MMO Roadmap

> The canonical cross-project ordering, acceptance gates and deferred
> network/scaling stage live in `docs/llm/gothic-mmo-roadmap.md`. This file is
> a compact client-local history and must not override the canonical roadmap.

This is a compact direction map for agents. Use `next-work.md` for the current
implementation edge.

## Phase 1 - Preserve Single-Player, Add MMO Opt-In

Status: mostly established.

- Keep native Gothic behavior unchanged by default.
- Gate MMO behavior behind explicit flags.
- Add client hooks that emit semantic actions/intents.
- Build C++ server receive/validate path.

## Phase 2 - Server Persistence and Bootstrap

Status: partially established.

- Server owns current character/world state in runtime DB.
- Server can create/read bootstrap materialization snapshots.
- Client can receive bootstrap ACK/snapshot chunks and write runtime artifacts.
- Save/load UX still needs final MMO character selection flow.

## Phase 3 - Content Build and Runtime Read-Model

Status: Step224-Step226 established.

- Parse Gothic content from install/VDF/ZEN/DAT/OU.
- Store parsed content in `mmo_content_build`.
- Export `mmo.content_build_runtime_read_model.v1`.
- Load and validate read-model in C++.
- Materialize C++ read-only indexes.

## Phase 4 - `world_instance` Content Cache

Status: established in two complementary server paths.

- The Step226 runtime read-model cache exposes indexed content-build records.
- The private-asset authority path exposes an immutable ZEN/WayNet/VOB definition.
- Typed trigger, mover, interactive, transition and spawn payload vectors are now
  bound to compact entity payload indices and included in the content fingerprint;
  typed spawn enable state is applied when the server materializes its entity registry.
- Typed mover keyframes, behavior, speed/interpolation modes and SFX identities are
  compiled server-side into deterministic segment/stay-open timing plans.
- Active content revision/world-instance admission still needs one production
  composition root; the full client must not load or interpret server content.

## Phase 5 - Read-Only NPC/Perception Assessment

Status: isolated logical foundation implemented; production world-host cutover pending.

- Enumerate active players/NPCs in world instance.
- Query nearby entities.
- Evaluate perception readiness/cooldowns.
- Write decisions to `mmo_ai_runtime`.
- Do not yet broadcast live movement/dialog.

## Phase 6 - Server NPC Tick and Dispatch

Status: partial isolated implementation; not yet the production UDP path.

- Server ticks routines/perception decisions.
- Server chooses actions.
- Server sends typed deltas/events to interested clients.
- Client presents results without owning truth.


## Current Server-Side Iteration - Typed ZEN World Logic

- ZenKit extraction now materializes typed trigger/contact, mover/keyframe,
  interactive/lock, level-transition and spawn metadata.
- Immutable admission resolves VOB target names to stable server entity IDs and
  rejects malformed references before a world is admitted.
- A dedicated server module binds contact-capable payloads to the deterministic
  trigger runtime and exposes world-start/external-trigger catalogs.
- No client authority, transport code or gameplay mutation was added.
- Target recursion, mutable mover state-machine execution, script dispatch and world
  transitions remain server work before the full-client adapter phase.

## Phase 7 - Full MMO UX

Status: target direction.

- MMO `Save` disabled/replaced by server persistence.
- MMO `New Game` creates a server-side character.
- MMO `Load Game` lists server-side characters.
- No dependency on local save files or hardcoded `PC_HERO`/`Ja` for final play.

## Phase 8 - Hardening

Status: future.

- Auth/session/character ownership.
- Content revision compatibility checks.
- Production DB shape.
- Performance profiling.
- Network interest management.
- Regression test matrix.
