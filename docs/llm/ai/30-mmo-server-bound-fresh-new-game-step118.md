# MMO server-bound fresh New Game - Step118

Step118 deliberately re-opens the `New Game` menu action while `-mmo-client-server`
is active.

## Why

DB Continue is still the authoritative restore path, but it restores
`character_script_state` from MySQL. After a clean import this can already contain
thousands of script rows, which is useful for continuing a DB character but wrong
when the developer wants to test a fresh vanilla New Game script baseline.

## Behavior

- `New Game` in server-bound mode starts `GameSession::StartupMode::MmoServerFreshNewGame`.
- That mode builds the normal local world/script baseline and runs first-time
  startup triggers.
- It does not request or apply a DB bootstrap snapshot during loading.
- `Load`/`Continue` still use the synthetic DB Continue slot and restore from
  the server snapshot.

Expected log:

```text
MMO menu New Game: starting fresh server-bound client baseline without DB Continue restore
MMO server-bound New Game: starting fresh local baseline without DB bootstrap snapshot
```

This does not delete MySQL rows. It only prevents New Game from importing DB
script/world snapshot state into the just-created local session.




