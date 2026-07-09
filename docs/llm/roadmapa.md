# 37 MMO Server Content Authority And NPC Perception Roadmap

Compact working roadmap. Source code and SQL migrations are the source of truth.

## Done

- C++ runtime read-model export, load and index probes.
- World-instance content cache and NPC perception policy probes.
- AI runtime decision/action queue schema and dispatcher boundary.
- Diagnostic `npc_greet_player` / `npc_warn_player` dialog-intent packet path.
- Disabled-by-default live diagnostic `ServerNpcDialogIntent` send and client
  ACK/NACK receive.
- In-memory delivery terminal/idempotency state.
- Optional AOI fanout selection for server-owned gameplay packets.
- Client no-apply dialog presentation, speaker resolution and presenter
  preflight probes.
- In-memory server conversation observer state.
- Late-observer resume plan, packet boundary, send gate, registration boundary,
  dispatch envelope, mutation guard, failure/dead-letter guard and commit
  preflight.
- Step273 SQL source plus local apply/checker validation for durable delivery,
  receipt, conversation observer and dead-letter storage.
- Step274 disabled-by-default C++ persistence preview bridge for Step273
  dialog-intent delivery/conversation SQL/procedure plans, with MySQL execution
  forced off.

## Not Done

- Executable C++ Step273 persistence calls / MySQL mutation from runtime.
- Real replay UDP send.
- Client dialog UI/audio application.
- `mark_applied` after durable ACK/apply/dead-letter contract.
- Active read-model auto-selection from `mmo_content_build`.
- Runtime NPC identity materialization for weak live rows.
- Default-off world-instance AI scheduler.
- Server-side Daedalus/script VM and shared dialog condition execution.
- NPC routine/path/movement simulation.
- Server-authoritative combat/spells/projectiles.

## Next

Next C++ work should add a guarded execution/result boundary around the
Step274 preview bridge while keeping `runMysql`, replay UDP send and
`mark_applied` disabled. Real DB/tools mutation stays deferred until the final
batch is intentionally applied.

