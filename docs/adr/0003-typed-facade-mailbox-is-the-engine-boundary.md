# ADR 0003 — Typed Facade Mailbox Is the Engine Boundary

Status: accepted

## Context

Letting wire packets, filesystem snapshots or transport callbacks reach
OpenGothic couples rendering code to protocol mechanics and creates competing
state paths.

## Decision

The public sandbox facade is the only MMO communication boundary. The full
client converts facade DTOs into protocol-independent presentation records and
drains them on the engine thread as:

1. optional route replacement;
2. zero or more atomic bootstraps;
3. live events ordered by stream sequence.

Invalid source records are rejected before engine mutation. String/JSON/file
bootstrap compatibility is not an alternative production path.

## Consequences

- Protocol changes are contained by the facade adapter.
- OpenGothic object mutation stays on its owning thread.
- Mailboxes and rejection accounting must remain bounded and observable.
- The compiled but unreachable legacy restore path must be removed rather than
  revived.
