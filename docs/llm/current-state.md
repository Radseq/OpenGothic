# Current State

Last verified context: Step274 persistence preview ZIP / 2026-07-09. Full
`server/cpp` build passed, `Gothic2Notr` target was up to date, runtime
read-model export/probe and server startup-check passed, and Step212/213/273
MySQL checkers passed against the local `$MYSQL_URL` target.

If this file conflicts with source code, inspect the code first.

## Active Goal

Turn OpenGothic/Gothic II NotR into a server-authoritative MMO while preserving
native single-player behavior unless explicit MMO flags are used.

## Authority Loop

```text
OpenGothic client
-> ASIO UDP binary packets
-> C++ MMO server
-> MySQL runtime/content schemas
-> server-owned projections/events
-> bootstrap snapshot / gameplay packets / ACK/NACK / diagnostics
```

The old Python receiver/worker path is debug/fallback debt. New gameplay work
targets the C++ server path.

## Content And AI Loop

```text
Gothic content/VDF
-> C++ ZenKit importer
-> parser_snapshot.json
-> runtime read-model JSON
-> C++ read-model indexes
-> WorldInstanceContentCache
-> NPC perception policy
-> mmo_ai_runtime decisions/action queue
-> disabled-by-default dialog-intent transport proof chain
```

Known read-model counts from the current snapshot:

- `world_zen_entities=24917`
- `waypoint_edges=3202`
- `npc_templates=730`
- `item_templates=835`
- `routines=1186`
- `perception_bindings=38`
- `dialog_infos=4139`
- `dialog_outputs=20826`

## Recent Steps

- Step224-Step230: runtime read-model export, C++ indexes/cache, startup cache,
  NPC perception policy and explicit AI runtime recording probe.
- Step231-Step234: runtime NPC identity guard, manual world-instance AI tick,
  evidence guard and startup-only dry-run hook.
- Step235-Step242: action queue, dispatcher validation, typed dialog intent,
  diagnostic packet/encoding, JSONL evidence and client fanout plan.
- Step243-Step253: no-send/no-receive proof chain from send boundary through
  terminal plan, guard, report, gate and package.
- Step254-Step255: read-only LLM DB ledger checker/exporter used while DB work
  was paused.
- Step256-Step272: disabled-by-default live diagnostic dialog-intent transport,
  in-memory delivery terminal state, optional client ACK/observation receipts,
  no-apply client presentation probes, AOI fanout selector, in-memory
  conversation observers and late-observer resume preflight. Real replay send,
  dialog UI/audio, durable apply and `mark_applied` remain disabled.
- Step273: SQL migration added and applied for durable gameplay delivery,
  ACK/NACK/observation receipts, conversation observers and dead-letter state;
  a focused Step273 checker validates the new DB surfaces.
- Step274: disabled-by-default C++ persistence preview bridge builds typed
  Step273 conversation/delivery/observer/receipt/dead-letter SQL/procedure
  plans from late-observer resume commit preflight, but `execute_mysql=off`,
  no `runMysql` call is wired and runtime DB mutation remains disabled.

## Stable Facts

- `-mmo-client-server host:port` opts the client into server-bound behavior.
- Without MMO flags, native saves/new game remain unchanged.
- Server-bound materialization is load-time restore, not live replication.
- Client remains presentation/input/prediction; server owns MMO truth.
- The current NPC AI path is explicit probe/evidence only; no automatic world
  scheduler is enabled.
- SQLite is not needed for current server content/read-model/AI tests; it is
  legacy baseline/oracle tooling.

## Main Gaps

- Live runtime NPC rows can still miss stable `npc_instance`; guarded AI probes
  reject them before recording decisions.
- Active read-model selection is still explicit via server flags, not selected
  from `mmo_content_build.content_build_runtime_exports`.
- NPC movement/path/routine simulation is not implemented.
- Full server-side Daedalus/dialog VM execution is not implemented.
- Dialog UI/audio application on the client is still probe-only.
- Step273 durable storage exists in SQL and was applied locally; only the
  Step274 C++ preview bridge targets it. Executable runtime DB mutation is
  still disabled.
- `mmo_ai_mark_npc_perception_action_applied` must remain blocked until durable
  ACK/observation/dead-letter policy is wired and verified.

## Current Next Edge

Add a guarded no-execute execution/result boundary for the Step274 persistence
preview bridge. Keep `runMysql`, replay send, client UI/audio and `mark_applied`
disabled until DB/tools changes are applied intentionally as one batch and the
durable ACK/rollback/idempotency behavior is verified.

