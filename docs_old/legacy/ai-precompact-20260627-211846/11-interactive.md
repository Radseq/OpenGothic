# Interactives And Containers

Files: `game/world/objects/interactive.h`, `game/world/objects/interactive.cpp`.

- `Interactive::getId()` is the stable VOB identity component for an interactive.
- Persist `stateId`, `stateCount`, `stateMask`, locked, cracked, and container inventory.
- `restorePersistentState` applies restored state through the interactive ownership API.
- `setMobState` can trigger animation/state transitions; do not replace it with direct field writes.
- `inventory()` owns chest/container contents.
- `needToLockpick` depends on keys, lock code, and cracked state; it is a gameplay rule, not just UI state.

Door/container events should eventually be emitted at interaction commit points, not inferred only from periodic snapshots.

