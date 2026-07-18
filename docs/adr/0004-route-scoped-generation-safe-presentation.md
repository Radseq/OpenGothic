# ADR 0004 — Presentation Is Route-Scoped and Generation-Safe

Status: accepted

## Context

Entity IDs, local object addresses and revisions can be reused across world
replacement, reconnect or respawn. Heuristic lookup risks applying stale state
to a different object.

## Decision

Every authoritative projection is scoped to exact route identity. Entity and
item references retain required generations; aggregate updates retain expected
revisions. Route replacement clears bindings, interpolation, correction
history, pending UI and other route-scoped caches before bootstrap activation.
Bootstrap installation is atomic and live updates are monotonic.

Stable local engine tokens may map server identities but never substitute for
them. Missing or stale identity fails closed.

## Consequences

- Reroute/resync cannot preserve accidental aliases.
- Despawn/replacement is exact even when numeric IDs repeat.
- Prediction and interpolation are disposable views over authoritative state.
- Capacity and stale-record rejection require explicit diagnostics/tests.
