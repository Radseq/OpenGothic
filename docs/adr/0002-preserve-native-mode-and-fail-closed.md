# ADR 0002 — Preserve Native Mode and Fail Closed

## Status

Accepted.

## Context

OpenGothic remains a usable single-player client. Server-bound startup,
however, must not silently execute local gameplay when the facade is missing,
authentication fails, a route is lost or bootstrap cannot be admitted.

## Decision

Native and server-bound modes share presentation code where safe but have
explicit state ownership.

- Native mode keeps local scripts, simulation, UI and saves.
- Server-bound mode uses the public sandbox facade and authoritative
  presentation contracts.
- Once server-bound mode is selected, timeout, rejection, lost route or missing
  facade is surfaced as an MMO failure/recovery state.
- Server-bound mode never falls back to native gameplay.

A disabled bridge may accept a harmless no-op only to keep native call sites
simple. Code requiring proof of network submission must check the explicit
submitted state and command token.

## Rationale

Silent fallback would let a temporary network or admission failure switch the
owner of gameplay state. Explicit modes preserve native compatibility while
making server-bound failure deterministic and observable.

## Alternatives

- **Require MMO runtime for every OpenGothic build:** rejected because native
  standalone use must remain independent.
- **Automatically continue locally after disconnect:** rejected because local
  simulation cannot become authoritative for a shared world.

## Scope and consequences

- Native builds do not require MMO transport or server assets.
- Server-bound save/load is roster/session-driven, not local-save authority.
- Startup and reconnect paths must expose actionable failure/recovery states.
- Tests and build validation must cover both native preservation and
  server-bound behavior when mode boundaries change.

## Evidence

- `src/client/CMakeLists.txt` enables the public sandbox facade only when its
  target exists and otherwise defines a standalone disabled mode.
- `src/client/game/game/mmoclientbridge.h` separates session lifecycle and
  typed submission from native engine code.
- `src/client/docs/llm/current-state.md` records the production mode
  composition and remaining orchestration proof gaps.
- `tests/unit/mmo_full_client_adapter_tests.cpp` covers selected facade/adapter
  behavior without claiming complete graphical proof.
