# Full Client Intent Submission Contract

## Purpose

Define how OpenGothic input and UI actions cross into the MMO runtime without
letting the full client encode wire packets, own transport reliability or claim
a gameplay result.

This contract applies only to server-bound mode. Native mode retains the
original local gameplay path. Selecting server-bound mode is explicit and does
not permit silent fallback after communication or admission failure.

## API and contracts

The engine-facing seam is declared by
`src/client/game/game/mmoclientbridge.h`. It exposes session lifecycle and
Protocol V2 domain submissions such as:

- movement;
- interaction;
- equip, unequip and use item;
- pickup, drop, split and merge stack;
- weapon-state and combat action;
- dialog choice.

The bridge adapts engine-domain values to the public
`<gothic/mmo/client_runtime_facade.h>` API. It must not expose packet structs,
codec buffers, ASIO endpoints or private sandbox modules to UI or `GameSession`.

Submission rules:

- movement is normalized intent, never an authoritative transform;
- entity and world interactions require exact server handles and generations;
- inventory operations require exact stack identity and the expected revision
  required by the public facade request;
- dialog selection carries the stable server choice ID and exact dialog
  revision;
- combat submission selects a typed action; HP, hit, damage, death and loot are
  server outcomes;
- code requiring proof of network submission checks the explicit submit status
  and token instead of treating a native-mode no-op as transmission.

Invalid or incomplete identity fails before gameplay mutation. A user-visible
pending indicator is allowed, but authoritative values change only after
server projection.

## Data and state

The full client may own:

- transient input samples;
- protocol-independent request DTOs;
- a bounded command token/pending presentation entry;
- user-visible submission, rejection and completion state.

The full client does not own:

- packet sequence, ACK/retry or reconnect state;
- session recovery truth;
- authoritative aggregate revisions;
- persistent inventory, combat, quest or world state.

Session and transport state remain inside `src/client_sandbox`. Authoritative
state returns through the presentation mailbox and read models, not through a
local mutation performed at submission time.

## Dependencies

Upstream owners:

- OpenGothic input, menu and UI controllers;
- exact engine-to-server identity bindings maintained by presentation state.

Downstream owner:

- the public `client_runtime_facade` implemented by `src/client_sandbox`.

Required decisions:

- [ADR 0001](../adr/0001-client-is-input-and-presentation-adapter.md);
- [ADR 0002](../adr/0002-preserve-native-mode-and-fail-closed.md);
- [ADR 0005](../adr/0005-server-revisioned-ui-has-no-optimistic-authority.md).

Forbidden dependencies include server-private headers, sandbox transport
modules, packet codecs and JSON command envelopes. The strict boundary checker
in `tools/check_client_mmo_sandbox_boundary.py` enforces these exclusions.

## Examples

### Server-bound inventory action

```text
inventory UI
  -> exact stack handle + expected revision
  -> submitProtocolV2EquipItem(...)
  -> pending indicator
  -> command completion and authoritative equipment/inventory projection
  -> UI refresh
```

The item is not removed or equipped locally before the authoritative update.

### Invalid interaction

```text
local object has no exact server generation
  -> bridge validation rejects the request
  -> no heuristic numeric-ID lookup
  -> no packet submission and no local gameplay result
```

### Native mode

```text
native input
  -> original OpenGothic gameplay path
  -> no requirement for sandbox transport or server availability
```
