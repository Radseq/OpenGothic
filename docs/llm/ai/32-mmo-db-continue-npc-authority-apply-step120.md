# MMO DB Continue NPC Authority Apply - Step120

Problem:
- Fresh server-bound New Game can run Xardas normally.
- After save, exit and DB Continue, the DB checkpoint path can still leave Xardas
  standing because the client only called `resumeAiRoutine()`.
- The current snapshot parser counted `npc_routine_state` rows but did not parse
  them into typed client data, so the rows could not be applied.
- The clean DB schema also did not contain the `mmo_npc_*_state_current` tables
  and `mmo_record_npc_*` procedures required by the Step117 observer.

Changes:
- Forced a wide NPC authority sample before server-bound save checkpoint
  manifest creation.
- Added parsing of `npc_routine_state` rows in `mmorestoresnapshot.h`.
- DB Continue now applies routine authority rows after the last
  `triggerOnStart(false)` reset by starting the recorded `script-fn:*` state on
  the matching `npc:<world>:pid:<id>:sym:<symbol>` NPC.
- Fresh server-bound New Game ignores unsolicited live DB snapshot refreshes, so
  a clean local baseline is not overwritten by an existing DB checkpoint.
- Added `server/sql/step120_npc_authority_restore_bridge.sql` for clean DBs.
- Clean MySQL rebuilds install Step120 by default through
  `run_mmo_step55_clean_mysql_from_pre_xardas.py` /
  `reset_mmo_mysql_from_chapter1_start.py`; use
  `--skip-step120-npc-authority-restore-bridge` only for regression/debug runs.
- Split/merge stack semantic actions are accepted as server no-ops until a
  dedicated stack layout model exists.

Expected logs after save + DB Continue:

```text
MMO server DB restore NPC routines resumed: ... npc_routine_state=N ... authority_applied=M
MMO DB continue startup NPC authority applied: routine_applied=M ... records=N
```

If `records=0`, apply the SQL bridge after cleaning the DB and verify that the
new server binary is running.




