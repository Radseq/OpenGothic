# Full Client Presentation Mailbox Contract

## Purpose

Define the single production path from the public sandbox facade into
protocol-independent full-client presentation state. The mailbox isolates
OpenGothic from wire formats, transport callbacks and bootstrap assembly.

## API and contracts

`ClientRuntimePresentationMailboxSnapshot` is drained from the public facade
and converted by
`src/client/game/game/mmoserverpresentationfacadeadapter.h` into a
`ServerPresentationMailboxBatch`.

A batch contains:

1. at most one route replacement;
2. zero or more complete bootstraps;
3. zero or more live presentation events;
4. a count of source records rejected during mapping.

The adapter validates facade DTOs before exposing them to engine state. Invalid
records increment `rejectedRecords` and are omitted. Valid live events are
stable-sorted by authoritative stream sequence before consumption.

`consumeServerPresentationBatch(...)` applies the batch in authority order:

1. route replacement;
2. bootstrap installation;
3. live events.

The sink is notified only after `ServerPresentationState` accepts the record.
Duplicate or stale records may be ignored without engine mutation; invalid
route, identity, revision or capacity conditions are observable rejections.

The engine thread performs the bounded drain. Network callbacks and sandbox
workers do not mutate OpenGothic objects directly.

## Data and state

The mailbox is transient delivery state. It does not replace the authoritative
projection owned by `ServerPresentationState` or the engine objects owned by
`GameSession`.

Owned batch data includes:

- optional route identity;
- immutable bootstrap DTOs;
- protocol-independent live-event variants;
- source-rejection accounting.

The mailbox must remain bounded by the facade/sandbox capacities. A consumer
must not retain unbounded history or duplicate the complete projected state.

Ordering invariants:

- route replacement precedes every record for the new route;
- bootstrap precedes live deltas that depend on its baseline;
- live events are monotonic by stream sequence;
- engine notifications follow successful state validation;
- a rejected source record cannot partially mutate presentation.

## Dependencies

Upstream owner:

- `src/client_sandbox/include/gothic/mmo/client_runtime_facade.h`, including its
  typed mailbox snapshot and bounded runtime queues.

Downstream owners:

- `mmoserverpresentationstate.h` for validation and accepted projection;
- `mmoserverpresentationbatchconsumer.h` for ordered application;
- `GameSession` materializers for engine-object changes after acceptance.

Required decisions:

- [ADR 0003](../adr/0003-typed-facade-mailbox-is-the-engine-boundary.md);
- [ADR 0004](../adr/0004-route-scoped-generation-safe-presentation.md).

Wire codecs, filesystem snapshots and JSON compatibility records are forbidden
as parallel production mailbox sources.

## Examples

### Normal drain

```text
facade snapshot
  -> map route
  -> validate/map complete bootstrap
  -> validate/map live records
  -> stable-sort live events by stream sequence
  -> replace route
  -> install bootstrap
  -> apply live events
  -> notify engine sink after each accepted state transition
```

### Invalid source record

```text
facade record contains an invalid handle or enum
  -> adapter rejects it
  -> rejectedRecords increments
  -> no presentation event is emitted
  -> existing valid projection remains unchanged
```

### Duplicate delivery

```text
already-applied route/bootstrap/event arrives again
  -> presentation state classifies duplicate or stale
  -> no second engine mutation
  -> processing continues with later valid records
```
