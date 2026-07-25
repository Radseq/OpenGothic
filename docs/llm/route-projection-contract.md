# Full Client Route and Projection Contract

## Purpose

Define how one exact authoritative route becomes validated client presentation
and then OpenGothic engine objects. The contract prevents stale reconnect,
respawn or world-replacement data from binding to reused local objects.

## API and contracts

`ServerPresentationState` owns the protocol-independent accepted projection.
Its public operations are centered on:

- `replaceRoute(...)`;
- `installBootstrap(...)`;
- `apply(...)` for live presentation events;
- exact lookup by server identity;
- `reset()` for complete local projection removal.

Route identity includes the connection/route epoch and exact world identity
with generation. Entity and item references retain required generations.
Aggregate-specific records retain their authoritative revision or baseline.

Route replacement rules:

- a newer valid route clears all previous route-scoped state before activation;
- duplicate route information is idempotent;
- stale or conflicting route information is rejected;
- local engine tokens never substitute for server identity.

Bootstrap rules:

- bootstrap belongs to the active route;
- all required sections are validated before the new baseline becomes active;
- installation is atomic from the presentation consumer's point of view;
- failure must not expose a partially installed route projection.

Live-event rules:

- event headers must match the active route and baseline constraints;
- stream sequence and aggregate revisions are monotonic;
- exact generations protect despawn/replacement and reused numeric IDs;
- configured capacities fail closed instead of growing without bound.

`GameSession` applies accepted records to engine objects. Its registries map
stable server handles to local object lifetimes, while movement interpolation
and correction remain disposable visual state.

## Data and state

`ServerPresentationState` owns the latest accepted route-scoped projection,
including bounded entity, NPC, combat, world-object, interactive, mover,
dialog and projectile presentation state.

`GameSession` owns:

- local engine objects and presentation components;
- identity registries binding exact server handles to local lifetimes;
- interpolation/correction samples;
- rendering, animation, audio and UI application.

Route replacement clears at least the route-scoped bindings, accepted
projection, interpolation/correction history and pending UI state associated
with the previous route. Native-mode objects follow their native ownership and
must not be treated as server replicas.

## Dependencies

Upstream contract:

- `presentation-mailbox-contract.md` supplies validated route/bootstrap/event
  order.

Downstream owners:

- `GameSession` MMO implementation units;
- entity/world-object registries;
- movement correction and interpolation boundaries;
- authoritative UI read models.

Required decisions:

- [ADR 0001](../adr/0001-client-is-input-and-presentation-adapter.md);
- [ADR 0004](../adr/0004-route-scoped-generation-safe-presentation.md).

The server remains the owner of world and gameplay truth. The full client may
interpolate or predict visuals but cannot resolve contacts, damage, inventory
or persistent outcomes.

## Examples

### World reroute

```text
route epoch 17 / world generation 4 is active
  -> route epoch 18 / world generation 5 arrives
  -> clear old bindings, samples, pending UI and projection
  -> install the new route bootstrap atomically
  -> apply only events matching the new route and baseline
```

### Reused numeric entity ID

```text
entity {id=42, generation=3} despawns
entity {id=42, generation=4} spawns later
stale event for generation=3 arrives
  -> exact-handle validation rejects the stale event
  -> generation=4 object is not mutated
```

### Visual prediction

```text
local movement input produces reversible visual prediction
  -> authoritative correction arrives for the active route/entity generation
  -> correction boundary validates it
  -> visual state converges without changing server gameplay truth
```
