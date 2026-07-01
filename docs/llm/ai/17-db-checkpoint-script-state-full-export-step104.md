# 17 DB Checkpoint Script-State Full Export - Step104

Log evidence after Step103:

- Runtime/server path was healthy: direct DB actions accepted, no rejected actions, strict DB checkpoint restore selected `snapshot_source=db_save_checkpoint_v1`.
- The Step103 checker still showed export coverage failure for script state: checkpoint table `mmo_save_checkpoint_script_state_snapshot` had thousands of rows, but exported bootstrap JSON `script_state` had only the small safe-int subset.
- This is not a transport failure. The export function intentionally filtered `script_state` to `value_type IN ('int','array_int')` because the current client restore path only safely applies integer Daedalus values.

Step104 contract:

- Keep `script_state` as the safe apply subset for the current client: `int` and `array_int` rows only.
- Add `script_state_full` to `mmo_bootstrap_snapshot_v1` for DB-native save checkpoint coverage. It contains all rows from `mmo_save_checkpoint_script_state_snapshot`, including text/real rows.
- Checkers must compare full checkpoint coverage against `script_state_full`, not against the safe apply subset.
- The client should not apply `script_state_full` yet. Full Daedalus restore needs a separate typed restore path and safety rules for strings/floats/arrays.

Operational note:

- `script_state_full` can materially increase DB-checkpoint bootstrap snapshot size. This is acceptable for strict DB-continue validation, but future production work should move this from JSON bootstrap to typed/binary domain snapshots or server memory materialization.
- Accepted `ready_weapon`/`holster_weapon` lines are log-coalesced like movement. Errors/diagnostics remain visible.
