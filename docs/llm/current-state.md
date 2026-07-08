# Current State

Last verified context: Step242 fanout plan verified / 2026-07-08.

If this file conflicts with source code, inspect the code first.

## Active Goal

Turn OpenGothic/Gothic II NotR into a server-authoritative MMO while preserving
native single-player behavior unless explicit MMO flags are used.

## Current Authority Loop

```text
OpenGothic client
-> ASIO UDP binary packets
-> C++ MMO server
-> MySQL procedures/read models
-> journal/current projections
-> bootstrap snapshot / ACK / NACK / diagnostics
```

The old Python receiver/worker path is debug/fallback debt. New gameplay work
should target the C++ server path.

## Current Content/AI Loop

```text
Gothic content/VDF
-> C++ ZenKit content importer
-> parser_snapshot.json
-> runtime read-model JSON
-> C++ read-model indexes
-> WorldInstanceContentCache
-> NPC perception policy
-> explicit mmo_ai_runtime recording/probes
-> disabled-by-default AI action preview/evidence boundaries
```

## Verified Build/Test Snapshot

Fresh read-model export from `runtime/content_build/parser_snapshot.json`:

- `world_zen_entities=24917`
- `waypoint_edges=3202`
- `npc_templates=730`
- `item_templates=835`
- `routines=1186`
- `perception_bindings=38`
- `dialog_infos=4139`
- `dialog_outputs=20826`

Verified on 2026-07-08:

- full `server/cpp` CMake build passed after adding missing Step232-242 targets;
- Step224/226 runtime read-model probe passed;
- Step227 cache probe passed;
- Step229 policy probe passed;
- Step230 synthetic dry-run passed;
- Step212 and Step213 DB checks passed;
- Step235 action queue read-only probe passed;
- Step236 dispatcher read-only probe passed;
- Step232/233 manual AI tick dry-run correctly rejected live NPC rows with
  missing `npc_instance`;
- Step234 `mmo_udp_server --startup-check-only --world-instance-ai-startup-dry-run`
  passed and did not write AI decisions;
- Step236-242 dispatcher chain passed with explicit claim, diagnostic preview,
  binary diagnostic encoding, JSONL evidence, client fanout plan and skip cleanup;
- final action queue probe reported `pending_count=0`.

## Step Summary

- Step224-230: read-model export, C++ indexes/cache, server startup cache,
  perception policy and explicit `mmo_ai_runtime` recording probe.
- Step231: runtime NPC actor-source rejects weak identity by default.
- Step232: manual `world_instance` AI tick probe, dry-run by default.
- Step233: evidence guard for manual AI tick.
- Step234: startup-only AI dry-run hook in `mmo_udp_server`.
- Step235: action queue inspection/claim probe, read-only by default.
- Step236: dispatcher contract validation, no live dispatch.
- Step237: typed descriptor for safe greeting/warning actions.
- Step238: log-only dialog intent preview.
- Step239: diagnostic packet contract.
- Step240: binary `ServerDiagnosticPacket` encode/decode check.
- Step241: JSONL durable evidence writer, explicit file-write flag required.
- Step242: client fanout plan builds for a session-bound greeting/warning action.

Step242 is still plan-only. It does not send UDP packets, open dialog UI/audio
or mark actions applied.

## Stable Facts

- `-mmo-client-server host:port` opts the client into server-bound behavior.
- Without MMO flags, native saves/new game remain unchanged.
- Server-bound materialization is load-time restore, not live replication.
- Client remains presentation/input/prediction; server owns MMO truth.
- Current NPC AI path is explicit probe/evidence only, not an automatic scheduler.
- No dialog UI/audio fan-out, movement replication, combat execution or
  `mmo_ai_mark_npc_perception_action_applied` path is enabled.

## Main Gaps

- Live DB sample still has NPC rows with missing `npc_instance`; guarded probes
  reject them before recording live decisions.
- SQLite is no longer needed for server content/read-model/AI tests, but remains
  a legacy baseline/oracle for old clean rebuild and save-to-DB migration paths.
- Active read-model selection is still explicit via server flags, not selected
  from `mmo_content_build.content_build_runtime_exports`.
- Live NPC movement/pathing/routine simulation is not implemented.
- Server script/dialog VM tick is not implemented.
- No live fanout/send/ACK contract exists after the Step242 plan.

## Current Next Edge

Design the next disabled-by-default boundary after Step242: either an explicit
no-send fanout adapter probe that resolves target sessions, or a client ACK
contract for when live diagnostic/dialog fanout is eventually allowed.
