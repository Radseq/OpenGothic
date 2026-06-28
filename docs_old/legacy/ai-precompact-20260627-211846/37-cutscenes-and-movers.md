# Cutscenes And Movers

Files: `game/world/triggers/cscamera.*`, `game/world/triggers/movercontroler.*`, `game/world/triggers/*`, `game/world/worldobjects.*`.

- `CsCamera` is a trigger-driven spline camera with delay, duration, and player-movement control.
- `MoverControler` forwards trigger messages and keys to movable world objects.
- `WorldObjects` owns the active cutscene pointer and dispatches trigger execution.
- Generic trigger persistence is covered by `25-perception-and-triggers.md`; this file identifies camera/mover-specific local timeline state.

Persist a mover's durable transform/state and idempotent trigger consequences. Do not restore an active cutscene clock or raw trigger pointer; clients may restart presentation from an authoritative event when needed.

