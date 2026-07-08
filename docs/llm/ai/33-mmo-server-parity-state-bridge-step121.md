# MMO Server Parity State Bridge - Step121

Purpose: make clean MySQL rebuilds install the durable DB state already expected
by the C++ UDP server for trigger queues, world transitions and client
corrections.

Changes:
- Added `server/sql/step121_server_parity_state_bridge.sql`.
- Added current/history projections for:
  - trigger queue / event timer state;
  - character world transition, visited-world and chapter state;
  - client action correction records after rejected authoritative actions.
- Added procedures used by the direct C++ server path:
  - `mmo_record_trigger_queue_state`;
  - `mmo_record_world_transition_state`;
  - `mmo_record_client_action_correction`;
  - `mmo_ack_client_action_correction`.
- Clean MySQL rebuilds install Step121 by default through
  `reset_mmo_mysql_from_chapter1_start.py` and
  `run_mmo_step55_clean_mysql_from_pre_xardas.py`.
- Added `check_mmo_step121_server_parity_state_bridge.py` to validate tables and
  procedures, with optional smoke calls for recorder/ack behavior.
- Added `tools/apply_current_mmo_db_state.py` as the current-state entrypoint for
  existing DBs, so normal work does not require manual StepXX SQL commands.

Important constraints:
- Step121 is not a full server-side world simulation tick.
- It closes the durable DB/server bridge for state that the C++ server already
  sends in snapshots or records from semantic actions.
- NPC routine/AI/path/fight tables and procedures are owned by Step120 in the
  current clean DB path.

Remaining work:
- Replace correction delivery through reused snapshot refreshes with typed live
  correction packets.
- Execute trigger timers from a deterministic server tick.
- Add real server-side NPC routine/pathing/AI/fight simulation and stream live
  NPC state instead of relying on observed/client-fed bridge rows.




