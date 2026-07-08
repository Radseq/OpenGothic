# MMO NPC Observation Fail-Open Item Refresh Guard - Step122

Problem:
- NPC authority observations fixed DB Continue routine wakeup, but a single
  rejected `record_npc_routine_state` could still produce a server correction
  snapshot.
- Correction snapshots are meant for authoritative rejected gameplay actions.
  They are wrong for best-effort NPC observation rows and can refresh the nearby
  world item window while the client is locally interacting with items.
- In practice this looked like a picked-up item appearing again.

Changes:
- MySQL command failures now include captured CLI output in the server log.
- NPC routine/AI/path/fight observation failures are accepted fail-open by the
  C++ UDP server. They log
  `[direct_db_observation_failed_accepted]` and do not queue a correction
  snapshot.
- Added SQL bridge to widen NPC authority key and idempotency columns to 512 and
  recreate the four `mmo_record_npc_*_state` procedures with matching parameter
  widths.

Gameplay authority remains strict for pickup/loot/drop/equip and similar item
mutations.




