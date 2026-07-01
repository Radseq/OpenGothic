# MMO DB World Clock Routine Bootstrap - Step115

Step115 fixes the next DB Continue parity gap visible around Xardas.

Problem:
- DB Continue built the physical ZEN baseline and ran script initialization.
- `initScripts(true)` ends in `wrld->resetPositionToTA()`, which chooses NPC TA
  states from the current game time.
- The client still defaulted to 08:00 before script/routine initialization even
  though the server snapshot already exported `world_clock`.
- That could leave routine NPCs such as Xardas in a wrong or inert baseline TA
  state compared with a native `.sav` load.

Changes:
- `mmo_bootstrap_snapshot_v1` client parsing now includes `world_clock`.
- DB Continue reads the pre-world server snapshot clock before `initScripts`.
- Bootstrap/live snapshot restore synchronizes local game time from
  `world_clock`.
- Server-bound `NEW_GAME` menu actions are no longer redirected to DB Continue;
  they are blocked in the menu. `Load/Continue` remains the DB Continue entry.

Expected logs:

```text
MMO DB continue pre-world clock selected: world_time_ms=...
MMO server snapshot world clock applied: world_time_ms=...
```

What is still missing for `.sav` parity:
- authoritative NPC routine/path/fight queues;
- server-side NPC simulation tick for walking, perception, reactions and combat;
- full active/invalid NPC array parity, including despawn/respawn policy;
- visited-world/current-world/chapter transition authority;
- trigger queue/event timer parity;
- full world inventory/container ownership beyond currently bridged samples;
- rollback/correction when a server ACK rejects already-local client action;
- camera/cutscene/dialog edge-state parity where native save stores transient UI
  state that should not become production server truth.

Do not persist raw pointers, animation pose, particles, audio or render/camera
internals as production MMO truth. For NPC behavior, build typed server domains:
routine schedule, route target, current waypoint/fp, AI state intent and combat
state.
