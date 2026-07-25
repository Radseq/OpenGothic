# ADR 0005 — Server-Revisioned UI Has No Optimistic Authority

## Status

Accepted.

## Context

Immediate local mutation can make inventory, equipment, combat and dialog feel
responsive, but diverges under rejection, duplicate delivery, reconnect or
concurrent server changes.

## Decision

Server-owned UI reads revisioned snapshots/deltas and submits commands with
exact identity and expected revision.

- A pending indicator may appear immediately.
- Authoritative values change only after accepted server projection.
- Completion receipts report command completion but do not replace the
  resulting state revision.
- Dialog selection carries the stable choice ID and exact dialog revision and
  does not close optimistically.
- Visual combat prediction must not mutate replicated HP, hit, damage or death.

Native UI may retain local mutation in native mode.

## Rationale

Separating pending UX from authoritative values preserves responsiveness
without creating a second aggregate state. Revision checks make stale input and
concurrent changes explicit.

## Alternatives

- **Mutate locally and roll back on rejection:** rejected because rollback is
  incomplete under reconnect, duplicate delivery and concurrent updates.
- **Treat an accepted receipt as the new state:** rejected because the receipt
  does not contain or replace the authoritative aggregate projection.
- **Read native engine containers in server-bound mode:** rejected because they
  are not the server-owned read model.

## Scope and consequences

- Rejection and stale-revision feedback are first-class UI states.
- Route replacement cancels or invalidates pending UI work.
- New server-owned screens require a read model, typed intent, completion and
  resync behavior.
- Missing authoritative payloads must be added across shared/server/sandbox
  before client UI invents them.

## Evidence

- `src/client/game/game/mmoserverinventoryreadmodel.h` separates revisioned
  inventory/equipment models from bounded pending-command state.
- `src/client/game/game/mmoserverpresentationstate.h` owns revisioned dialog
  choices and route-scoped state.
- `src/client/game/game/gamesession_mmo_inventory.cpp` applies accepted
  inventory/equipment presentation.
- `tests/unit/mmo_server_inventory_read_model_tests.cpp` covers focused
  revision, pending, completion and rejection behavior.
