# Full Client Architecture

## Modes

Native mode runs the original OpenGothic simulation, scripts, saves and UI.
Server-bound mode keeps the same renderer and interaction surfaces but replaces
local gameplay decisions with intent submission and authoritative projection.
Mode selection is explicit; failure in server-bound mode is surfaced rather
than converted into native gameplay.

## Client-to-server path

```text
input/UI -> engine-domain intent -> client bridge -> public sandbox facade
         -> sandbox validation/transport -> server
```

The bridge adapts engine data and tracks user-visible submission/completion. It
does not encode packets, own retry state or invent a result.

## Server-to-client path

```text
sandbox facade mailboxes -> validated presentation adapter
 -> route/bootstrap/live-event state -> GameSession projection
 -> engine objects/read models/UI
```

A route replacement invalidates all route-scoped local bindings. Bootstrap is
atomic. Live records are monotonic and identity/revision checked. Engine-object
tokens are local implementation details and never replace server identity.

## State ownership

- facade/sandbox: connection, session, retry, ACK, resync and wire decoding;
- presentation state: last accepted authoritative projection and revisions;
- `GameSession`: engine-object ownership and visual application;
- UI: transient selection and pending indicators, never authoritative values;
- server: all gameplay and persistent state.

The worker/facade may run independently; OpenGothic object mutation occurs on
the engine thread after bounded mailbox drain. Avoid holding the bridge mutex
while performing rendering or expensive materialization.

Durable rationale is in [`../adr/README.md`](../adr/README.md).
