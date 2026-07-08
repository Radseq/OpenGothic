Step123 wires the Step122 NPC observation hotfix into the destructive clean DB flow.

Problem:
- After clean DB and live play, NPC authority samples could fail in direct MySQL mode.
- The server then queued a client correction/live snapshot for an observation-only action.
- That snapshot could refresh active world items and make a recently picked item appear again.

Change:
- `tools/bootstrap/reset_mmo_mysql_from_chapter1_start.py` now installs
  `server/sql/step122_npc_observation_failopen_and_item_refresh_guard.sql` by default,
  directly after Step120/Step121.
- `tools/bootstrap/run_mmo_step55_clean_mysql_from_pre_xardas.py` exposes
  `--skip-step122-npc-observation-failopen-item-refresh-guard`, but normal clean runs no
  longer require a separate manual SQL command.
- Step122 widens the NPC routine/AI/path/fight observation key/idempotency fields and
  recreates the recorder procedures with matching argument widths.

Expected after a clean rebuild:
- the reset manifest includes Step122 as an applied SQL surface;
- `record_npc_routine_state`/`record_npc_ai_state` observation packets should not fail
  because of canonical key width;
- item pickup persistence should no longer be undone by an NPC-observation correction
  snapshot.




