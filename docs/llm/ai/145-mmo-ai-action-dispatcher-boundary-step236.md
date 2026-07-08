# Step236 - AI Action Dispatcher Boundary

Purpose: validate queued NPC perception action rows as a future dispatcher
contract without executing effects.

Changed:

- `server/cpp/mmo_ai_runtime_persistence.*`
- `server/cpp/mmo_npc_perception_action_dispatcher_boundary.*`
- `server/cpp/mmo_npc_perception_action_dispatcher_probe.cpp`

Contract:

- Boundary classifies known `npc_*` action kinds and validates payload shape,
  stable IDs, target requirements and idempotency key.
- Read-only inspection reports valid/invalid contract counts.
- Claim validation is explicit and mutating only with consent flags.
- `live_dispatch_implemented=false` and `live_dispatch_executed=false`.

No `mmo_ai_mark_npc_perception_action_applied`, packet fan-out, dialog, movement
or combat side effect is executed.
