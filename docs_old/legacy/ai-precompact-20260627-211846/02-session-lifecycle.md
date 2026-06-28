# Session Lifecycle

`game/game/gamesession.cpp` owns `MmoRuntimeSqlite`.

1. Construct the world and script state first.
2. Create/open SQLite only after the world is available.
3. Restore DB state only after baseline world objects exist.
4. Call `tick()` from `GameSession::tick` for incremental capture.
5. Call the public `flush()` at controlled destruction for the full canonical projection.

Do not read or mutate live game objects from a background DB thread. Snapshot on the game thread first; only detached immutable data may be written asynchronously later.

