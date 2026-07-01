# MMO DB Server AI Authority Foundation - Step116

Purpose: move the remaining `.sav` parity gaps from loose future-work notes into
typed DB/server contracts that can be consumed by the C++ UDP server and future
server ticks without changing native single-player behavior.

Changes:
- Adds Step116 SQL tables and procedures for:
  - NPC routine state;
  - NPC AI intent/perception state;
  - NPC path/routing state;
  - NPC fight state;
  - trigger queue/timer state;
  - world/chapter/visited-world transition state;
  - client action correction/rollback state.
- Adds new semantic action kinds for those typed domains.
- Live/bootstrap snapshots now expose fail-soft sections:
  `npc_routine_state`, `npc_ai_state`, `npc_path_state`, `npc_fight_state`,
  `trigger_queue`, `world_transition_state` and `client_corrections`.
- Movement rejection now records a DB correction and queues a live snapshot.
  The client parses `client_corrections` and, on live refresh, rolls HERO back
  to the authoritative DB position/yaw when a pending correction is present.

Important constraints:
- This is not full server-side NPC simulation yet.
- The old `.sav` path remains unchanged unless `-mmo-client-server` is active.
- New snapshot queries are optional/fail-soft, so an older DB continues to
  bootstrap without the Step116 tables.
- Do not persist raw engine pointers, animation pose, audio/render/camera state
  or transient UI as production MMO truth.

What this unlocks next:
- A server tick can now write routine/path/AI/fight state as typed rows instead
  of debug JSON.
- Trigger timers can become DB-authoritative instead of purely local queues.
- Client rollback has a concrete DB-backed correction slice instead of only an
  ACK/NACK log.
