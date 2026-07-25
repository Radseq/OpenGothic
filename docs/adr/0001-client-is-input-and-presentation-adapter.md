# ADR 0001 — Client Is an Input and Presentation Adapter

## Status

Accepted.

## Context

An MMO client cannot be trusted with gameplay outcomes, while OpenGothic must
still own rendering, audio, animation, UI and local engine-object lifetime.
Without an explicit boundary, native engine containers and convenience logic
could become a competing source of HP, inventory, combat or world truth.

## Decision

In server-bound mode the full client submits player intent and projects
validated authoritative output.

- Server gameplay and persistence truth remains in `src/server`.
- Authority-neutral contracts remain in `src/shared`.
- Communication/session state remains in `src/client_sandbox`.
- The full client owns input adaptation, presentation state, engine-object
  materialization and transient UI state.

The client may perform reversible visual prediction and interpolation. It may
not commit HP, inventory, equipment, quests, prices, combat outcomes, NPC
behavior, persistent state or shared world state.

## Rationale

This keeps one authoritative mutation path while preserving OpenGothic's mature
presentation stack. Corrections, rejection, reconnect and resync can replace a
disposable local view without reconciling two gameplay simulations.

## Alternatives

- **Run matching gameplay locally and reconcile later:** rejected because the
  client is untrusted and deterministic parity is not authority.
- **Move rendering/UI ownership to the server or sandbox:** rejected because
  those layers must remain headless and engine-independent.

## Scope and consequences

- New gameplay starts on the server and crosses typed contracts before UI.
- Client-side state is a cache/read model, never recovery truth.
- Server rejection, correction, reroute and resync are normal control flow.
- Direct server-private or transport ownership in the full client is a boundary
  violation.
- Native single-player authority remains unchanged outside server-bound mode.

## Evidence

- `src/client/game/game/mmoclientbridge.h` exposes typed intent/session seams.
- `src/client/game/game/mmoserverpresentationstate.h` owns accepted client
  projection rather than gameplay mutation.
- `src/client/CMakeLists.txt` links only the public sandbox transport target.
- `tools/check_client_mmo_sandbox_boundary.py` rejects server/transport/codec
  ownership in the full client.
