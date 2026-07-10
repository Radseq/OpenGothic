# MMO Roadmap

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

Status: current next work.

- Create immutable per-world-instance content cache.
- Bind active content revision and world name.
- Provide read-only query APIs for NPCs, items, routines, perception, dialogs,
  waypoints and world entities.

## Phase 5 - Read-Only NPC/Perception Assessment

Status: not implemented.

- Enumerate active players/NPCs in world instance.
- Query nearby entities.
- Evaluate perception readiness/cooldowns.
- Write decisions to `mmo_ai_runtime`.
- Do not yet broadcast live movement/dialog.

## Phase 6 - Server NPC Tick and Dispatch

Status: not implemented.

- Server ticks routines/perception decisions.
- Server chooses actions.
- Server sends typed deltas/events to interested clients.
- Client presents results without owning truth.

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
