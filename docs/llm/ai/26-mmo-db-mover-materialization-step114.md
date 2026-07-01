# MMO DB Mover Materialization - Step114

Step114 closes the visible gap where MySQL already stored a mover/door/gate as
open, but DB Continue still showed the local baseline ZEN state.

Changes:
- server-bound New Game is blocked again at `MainWindow::startGame`, not only in
  the menu script action rewrite;
- DB Continue menu action rewrite is allowed from the in-game menu as well as
  the front menu;
- bootstrap/live snapshot apply now materializes `mover_state` onto
  `MoveTrigger`;
- restore uses the persistent mover key format emitted by the hook:
  `mover:<world>:<vob_id>:<vob_name-or-fallback>`;
- accepted `open`, `close` and `single_key` rows are projected to their
  `target_frame_index` and then set idle, because the DB event is captured when
  the trigger is accepted, before the local animation reaches its final frame;
- HERO DB-position restore clears residual velocity and updates the transform so
  the camera starts from the restored character transform instead of chasing a
  stale New Game transform.

Expected client log after DB Continue:

```text
MMO server snapshot world state applied: ... mover_state=N parsed_mover_state=N applied_movers=N missing_movers=0 skipped_movers=0 ...
```

If `missing_movers` is non-zero, the DB row exists but the persistent key does
not match a loaded `MoveTrigger` in the current world. Fix the identity mapping
before adding more fallback behavior.

This is still not full native-save parity. NPC AI/routines/path queues and full
server simulation are separate domains. Step114 only makes DB mover rows affect
the local world during DB Continue and live snapshot refresh.
