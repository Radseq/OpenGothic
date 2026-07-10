# Architecture Decisions

This file records durable decisions. Keep consequences short; implementation
details belong in source, tests or task notes.

## Server Is MMO Authority

Decision:

- The client is not source of truth for MMO world state, NPC decisions, dialogs or perception.

Reason:

- Multiple players must observe the same authoritative world.

Consequence:

- Client hooks produce intents/evidence.
- Server validates and journals accepted mutations.
- Client remains presentation/input/prediction in MMO mode.

## Preserve Native Single-Player

Decision:

- Old client behavior must keep working unchanged.
- MMO/server behavior must be opt-in through explicit flags or parameters.

Reason:

- OpenGothic must keep existing Gothic behavior for normal play.

Consequence:

- New systems must be additive server-bound paths, not replacements for the default client path.

## Split Content Build From Runtime

Decision:

- Parsed ZEN/DAT/OU content goes to `mmo_content_build`, not runtime gameplay DB.

Reason:

- Content parsing is build-time/static; runtime DB is live mutable state.

Consequence:

- Parser tools write content build snapshots/imports.
- Runtime/server cache consumes approved read-model exports.

## Use Runtime Read-Model Before NPC Tick

Decision:

- Server NPC/script work must first consume a shared runtime read-model.

Reason:

- Running per-client script truth would break MMO authority.

Consequence:

- Step224 exports `mmo.content_build_runtime_read_model.v1`.
- Step225 validates that read-model in C++.
- Step226 materializes read-only C++ runtime indexes.
- Next work is `world_instance` cache/tick, using those indexes as input.

## Keep Outbox As Fallback

Decision:

- `mmo_server_action_outbox` remains debug/fallback only.

Reason:

- Direct C++ server validation and DB procedures are the current authority path.

Consequence:

- Do not build new gameplay features around Python worker/outbox dependency.

## Stable Identity Over Labels

Decision:

- Durable identity uses world/content revision/persistent id/symbol/VOB/UUID, not display names.

Reason:

- Gothic saves and runtime objects can drift in labels, order and position.

Consequence:

- Resolver fallbacks may use bounded symbol/position evidence.
- Never rely on display-label-only matching.

## JSON Is Not Hot Gameplay State

Decision:

- JSON is acceptable for diagnostics, raw evidence and generated read-model artifacts, but not as the final hot gameplay contract.

Reason:

- Server authority needs typed validation, indexed columns and predictable performance.

Consequence:

- Prefer typed packets/procedures/current projections for live gameplay.
- Keep JSON compatibility where consumers still require it.

## DB Changes Should Be Explicit

Decision:

- Schema/procedure changes should be staged as explicit SQL/tool changes and documented with the reason.

Reason:

- Agents can otherwise silently drift runtime DB contracts.

Consequence:

- If the user asks not to apply DB changes, record intended SQL changes separately.
- If the user allows DB/tool updates, update the tool/check path together with schema/procedure changes.
