# Game Session

Files: `game/game/gamesession.h`, `game/game/gamesession.cpp`.

- `GameSession(std::string)` creates New Game: script, world, player, `postInit`, initial scripts, start triggers, camera, then optional SQLite open.
- `GameSession(Serialize&)` loads the normal save first, then optionally opens SQLite for DB restore.
- `save()` writes session clock, world, perception, quests, Daedalus variables, and camera.
- `tick()` advances game time, ticks script/world, then calls `MmoRuntimeSqlite::tick` last.
- The destructor calls the public SQLite `flush()`.
- `updateDialog()` and `dialogExec()` are the dialog journal hook sites.

Do not initialize DB restore before `World` and `GameScript` are ready.

