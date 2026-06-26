# AI And Movement

Files: `game/world/aiqueue.h`, `game/game/movealgo.h`, `game/game/fightalgo.h`.

- `AiQueue` stores pending commands with raw pointers to NPCs, items, and waypoints.
- `MoveAlgo` owns local physical movement, falling, climbing, swimming, collision, and short-lived path state.
- `FightAlgo` owns local combat decisions and target-distance instructions.

These structures are unsafe to restore directly from database rows because they contain process-local pointers and frame-dependent state. Restore only stable NPC state plus validated follow/escort intent; let AI rebuild its own queue after world load.

