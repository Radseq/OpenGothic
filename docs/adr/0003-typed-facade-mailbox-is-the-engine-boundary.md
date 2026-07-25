# ADR 0003 — Typed Facade Mailbox Is the Engine Boundary

## Status

Accepted.

## Context

Allowing wire packets, filesystem snapshots or transport callbacks to reach
OpenGothic would couple rendering code to protocol mechanics and create
competing restore/state paths.

## Decision

The public sandbox facade is the only production MMO communication boundary.
The full client converts facade DTOs into protocol-independent presentation
records and drains them on the engine thread in this order:

1. optional route replacement;
2. zero or more complete bootstraps;
3. live events ordered by authoritative stream sequence.

Invalid source records are rejected before engine mutation. The engine sink is
notified only after presentation state accepts the transition. String, JSON or
file-bootstrap compatibility is not an alternative production path.

## Rationale

One typed boundary contains protocol churn, preserves engine-thread ownership
and makes ordering, rejection and bounded delivery testable without the
renderer or private assets.

## Alternatives

- **Decode packets in `GameSession`:** rejected because it mixes protocol and
  engine ownership.
- **Retain file/JSON restore beside the mailbox:** rejected because it creates
  a second activation path with different validation and recovery semantics.
- **Mutate objects from transport callbacks:** rejected because OpenGothic
  object lifetime belongs to the engine thread.

## Scope and consequences

- Protocol changes are contained by the facade adapter.
- Mailboxes, capacities and rejection accounting remain bounded and
  observable.
- Route/bootstrap/event ordering is part of the engine boundary contract.
- Compiled but unreachable legacy restore code is removal debt, not a supported
  path.

## Evidence

- `src/client_sandbox/include/gothic/mmo/client_runtime_facade.h` declares the
  public typed mailbox snapshot.
- `src/client/game/game/mmoserverpresentationfacadeadapter.h` validates/maps
  records and stable-sorts live events.
- `src/client/game/game/mmoserverpresentationbatchconsumer.h` applies route,
  bootstrap and events in authority order.
- `tests/unit/mmo_full_client_adapter_tests.cpp` and
  `tests/unit/mmo_server_entity_presentation_registry_tests.cpp` cover selected
  adapter and presentation invariants.
