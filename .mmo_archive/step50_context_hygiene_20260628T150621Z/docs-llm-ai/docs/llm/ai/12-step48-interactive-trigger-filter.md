# 12 Step48 Interactive Trigger/Mover Filter

Real evidence from Xardas tower showed Step47 was directionally correct but too broad:
- the fireplace interaction appeared as `use_interactive` plus `update_interactive_state`;
- hundreds of unrelated `update_interactive_state` rows were emitted for mobsi bootstrap/TA materialization;
- the actual hidden gate/grate mechanism still needs trigger/mover evidence.

Changes:
- `onInteractiveStateChanged` now requires a player cause. Allowed causes are direct player actor, recent use of the same interactive, or a short recent player interactive world-cause window. Actor-null state changes with no player cause are suppressed.
- `update_interactive_state` payloads include `capture_cause` and `player_caused` so the checker can detect accidental bootstrap spam.
- Added semantic action kinds `trigger_event` and `mover_state_changed`. They are capture-only for now.
- `AbstractTrigger::implProcessEvent` emits `trigger_event` only when the recent player-interaction window is active. Startup/ambient trigger processing is not captured.
- `MoveTrigger::onTrigger`, `onUntrigger`, and `onGotoMsg` emit `mover_state_changed` for player-caused mover state starts. Continuous animation frames are not emitted.

Production meaning:
- Interactives remain canonical world entity state when `mmo_update_interactive_state(...)` applies.
- Trigger and mover events are evidence for hidden gates/doors/grates and should later become a DB procedure/projection pair such as `mmo_record_trigger_event(...)` and `mmo_record_mover_state(...)`.
- Do not persist per-frame mover animation. Persist accepted state transitions and final/current projection only.

Known limitations:
- Delayed trigger chains longer than the short player-cause window may need an explicit causal token propagated through `TriggerEvent`.
- Capture is intentionally conservative; if an indirect gate still does not appear, add cause propagation instead of reopening global mobsi state capture.
