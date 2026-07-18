# ADR 0002 — Preserve Native Mode and Fail Closed

Status: accepted

## Context

OpenGothic remains a usable single-player client, but server-bound startup must
not silently execute local gameplay when communication or admission fails.

## Decision

Native and server-bound modes share presentation code where safe but have
explicit state ownership. Native mode keeps local scripts, simulation, UI and
saves. Once server-bound mode is selected, missing facade, timeout, rejection or
lost route is surfaced as an MMO failure/recovery state; it does not fall back
to native gameplay.

A disabled bridge may accept a no-op solely to keep native call sites simple.
Code requiring proof of network submission must check the explicit submitted
state and token.

## Consequences

- Native builds remain independent of MMO transport and server assets.
- Server-bound save/load is roster/session-driven, not local-save authority.
- Tests must cover both native preservation and server-bound failure behavior.
