# ADR 0004 — Presentation Is Route-Scoped and Generation-Safe

## Status

Accepted.

## Context

Entity IDs, local object addresses and revisions can be reused across world
replacement, reconnect or respawn. Heuristic lookup risks applying stale state
to a different object.

## Decision

Every authoritative projection is scoped to exact route identity.

- Entity and item references retain required generations.
- Aggregate updates retain expected revisions or baseline metadata.
- Route replacement clears bindings, interpolation, correction history,
  pending UI and other route-scoped caches before new bootstrap activation.
- Bootstrap installation is atomic.
- Live updates are monotonic and validated against the active route.

Stable local engine tokens may map server identities but never substitute for
them. Missing, stale or conflicting identity fails closed.

## Rationale

Generations and route epochs make stale delivery distinguishable from valid
reuse. Atomic replacement prevents old and new worlds from being visible in one
client projection.

## Alternatives

- **Bind by numeric ID only:** rejected because IDs may be reused.
- **Keep old objects until matching updates arrive:** rejected because stale
  bindings can receive new-world state.
- **Repair mismatches heuristically:** rejected because ambiguity must not
  mutate a potentially different entity.

## Scope and consequences

- Reroute/resync cannot preserve accidental aliases.
- Despawn/replacement is exact even when numeric IDs repeat.
- Prediction and interpolation are disposable route-scoped views.
- Capacity and stale-record rejection require explicit diagnostics and tests.

## Evidence

- `src/client/game/game/mmoserverpresentationstate.h` validates route,
  bootstrap, event headers, exact handles and bounded maps.
- `src/client/game/game/mmoserverentitypresentationregistry.h` owns exact
  server-to-local binding.
- `src/client/game/game/mmoserverentityinterpolator.cpp` and
  `mmomovementcorrectionboundary.cpp` enforce route/identity-safe visual state.
- Focused evidence is in
  `tests/unit/mmo_server_entity_presentation_registry_tests.cpp`,
  `tests/unit/mmo_server_entity_interpolator_tests.cpp` and
  `tests/unit/mmo_movement_correction_boundary_tests.cpp`.
