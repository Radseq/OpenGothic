# Full Client Architecture

## Purpose

Describe the full client's server-bound architecture and its separation from
native OpenGothic. The full client owns presentation and engine integration;
all gameplay truth remains server-authoritative.

## API and contracts

Two explicit modes share presentation code where safe:

- **native mode** runs original scripts, simulation, saves and local UI;
- **server-bound mode** submits typed intent through the public sandbox facade
  and projects accepted server output.

Client-to-server flow:

```text
input/UI
  -> engine-domain intent
  -> full-client bridge
  -> public client_sandbox facade
  -> sandbox session/transport
  -> authoritative server
```

Server-to-client flow:

```text
public facade mailbox
  -> protocol-independent adapter
  -> route/bootstrap/live-event state
  -> GameSession materialization
  -> engine objects/read models/UI
```

Focused semantics are owned by:

- `intent-submission-contract.md`;
- `presentation-mailbox-contract.md`;
- `route-projection-contract.md`;
- `authoritative-ui-contract.md`.

## Data and state

| Owner | State |
|---|---|
| `src/client_sandbox` | connection, session, retry, ACK, reconnect, bootstrap assembly and wire decoding |
| Presentation state | last accepted route-scoped projection and authoritative revisions |
| `GameSession` | engine-object lifetime and visual application |
| UI | transient selection and pending indicators |
| Server | gameplay, persistence and recovery truth |

OpenGothic object mutation occurs on the engine thread after a bounded mailbox
drain. The bridge/facade may have independent worker activity, but rendering or
expensive materialization must not run while holding its synchronization
boundary.

Prediction and interpolation are reversible presentation state. Route
replacement clears route-scoped bindings and pending state before the new
bootstrap is activated.

## Dependencies

Allowed direction:

```text
full-client UI/engine
  -> public client_sandbox facade
  -> shared contracts
  -> server authority
```

The reverse direction is forbidden. Full-client production code must not own
ASIO, packet codecs, private sandbox modules, server gameplay headers or a local
durable gameplay database.

Durable rationale is indexed in [`../adr/README.md`](../adr/README.md).
Production composition is mapped in `repo-map.md`; evidence levels and gaps are
kept in `current-state.md`.

## Examples

### Server-bound startup

```text
menu selects server-bound mode
  -> connect/authenticate or resume
  -> obtain roster and enter world
  -> receive typed route/bootstrap
  -> materialize presentation
```

Failure remains an explicit MMO failure/recovery state and does not switch to
native gameplay.

### Native startup

```text
standalone/native configuration
  -> no sandbox facade requirement
  -> original OpenGothic simulation and saves remain active
```
