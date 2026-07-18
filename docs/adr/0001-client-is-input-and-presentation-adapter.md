# ADR 0001 — Client Is an Input and Presentation Adapter

Status: accepted

## Context

An MMO client cannot be trusted with gameplay outcomes, while OpenGothic still
must own rendering, audio, animation, UI and local engine-object lifetime.

## Decision

In server-bound mode the full client submits player intent and projects
validated authoritative output. Server gameplay truth remains in `src/server`;
neutral contracts remain in `src/shared`; communication state remains in
`src/client_sandbox`.

The client may perform reversible visual prediction and interpolation. It may
not commit HP, inventory, equipment, quests, prices, combat outcomes, NPC
behavior, persistent state or shared world state.

## Consequences

- New gameplay begins on the server and crosses typed contracts before UI.
- Client-side convenience state is a cache/read model, never recovery truth.
- Server rejection, correction, reroute and resync are normal control flow.
- Direct server-private or transport ownership in the full client is a boundary
  violation.
