# 25 MMO In-Session Menu DB Continue - Step113

Observed after Step112:

- Out-of-game DB Continue used the explicit `MmoDbContinue` startup mode and
  suppressed startup videos.
- The in-session menu could still open native save-slot UI or start the old New
  Game flow because the menu guard only applied while no game session was active.
- The server/client log showed a later bootstrap with
  `reason=new_game_pre_start_loaded` after an earlier DB Continue bootstrap.
- HERO position was restored from the DB snapshot, but the visual transform was
  not refreshed immediately after `setPosition`/`setDirectionY`.

Step113 rule:

- In server-bound mode, local New Game/Load slot flows are never the normal
  character-entry path, even from the in-game menu.
- Native single-player menus remain unchanged without `-mmo-client-server`.

Changed behavior:

- `GameMenu` now redirects New Game/load-style actions to DB Continue whenever
  `-mmo-client-server` is active, not only while out of game.
- DB snapshot position restore now clears stale movement and updates HERO's
  transform immediately after applying DB position/yaw.

Expected evidence:

```text
MMO menu Continue: loading server DB character without native save slot
MMO server snapshot restore scheduled ... reason=db_continue_baseline_loaded ...
```

There should be no follow-up `reason=new_game_pre_start_loaded` from normal MMO
menu use.
