# API Contracts

## MMO Flags

Core client/server flags:

- `-mmo-client-server host:port` enables server-bound client mode.
- `-mmo-server-endpoint host:port` is the endpoint alias/config path.
- `-mmo-action-session-key KEY` selects the client/server session key.
- `--runtime-read-model PATH` or `--runtime-read-model-path PATH` loads
  `mmo.content_build_runtime_read_model.v1`.
- `--content-revision-key`, `--world-instance-key`, `--world-name` bind the
  server cache to an explicit content/world instance.
- `--startup-check-only` validates startup/cache and exits before the UDP loop.

Diagnostic dialog-intent server flags:

- `--enable-ai-dialog-intent-send`
- `--ai-dialog-intent-fanout-mode target_session|aoi`
- `--ai-dialog-intent-aoi-radius N`
- `--ai-dialog-intent-max-recipients N`
- `--ai-dialog-intent-conversation-session-probe`
- `--ai-dialog-intent-observation-receipt-probe`
- `--ai-dialog-intent-late-observer-resume-plan`
- `--ai-dialog-intent-late-observer-resume-packet-boundary`
- `--ai-dialog-intent-late-observer-resume-send-gate`
- `--ai-dialog-intent-late-observer-resume-delivery-registration-boundary`
- `--ai-dialog-intent-late-observer-resume-dispatch-envelope`
- `--ai-dialog-intent-late-observer-resume-mutation-guard`
- `--ai-dialog-intent-late-observer-resume-send-failure-dead-letter-guard`
- `--ai-dialog-intent-late-observer-resume-commit-preflight`
- `--ai-dialog-intent-step273-persistence-preview`

Client probe flags:

- `-mmo-client-dialog-presentation-validate`
- `-mmo-client-dialog-presentation-main-thread-probe`
- `-mmo-client-dialog-main-thread-observation-receipt`

All MMO/server behavior requires explicit flags. Native single-player behavior
must remain unchanged without them.

## Runtime Read-Model

Schema: `mmo.content_build_runtime_read_model.v1`.

Required sections: `world_zen_entities`, `waypoint_edges`, `npc_templates`,
`item_templates`, `routines`, `perception_bindings`, `dialog_infos`,
`dialog_outputs`.

The C++ loader owns typed indexes for world entities, waypoint routes,
NPC/item templates, routines, perception bindings and dialog info/output names.
Cache loading is read-only: it does not execute scripts and does not mutate DB.

## NPC Perception And AI Runtime

Important modules:

- `mmo_npc_perception_policy`
- `mmo_npc_perception_runtime_source`
- `mmo_ai_runtime_persistence`
- `mmo_world_instance_ai_tick`
- `mmo_npc_perception_action_dispatcher_boundary`

Rules:

- Policy consumes `WorldInstanceContentCache` and explicit actor sets.
- Missing perception binding produces no decision.
- Recording requires stable `world_instance_uuid`, NPC identity, target key,
  perception kind and idempotency key.
- Live actor-source rejects weak NPC identity by default.
- Manual AI tick is dry-run by default; writes require explicit flags.
- The dispatcher must not call `mmo_ai_mark_npc_perception_action_applied`
  until live-send/ACK/apply/dead-letter storage is durable.

## Dialog Intent Proof Chain

Supported safe action kinds: `npc_greet_player`, `npc_warn_player`.

Current chain:

- typed effect descriptor and dialog intent preview;
- diagnostic packet contract and binary encoder;
- JSONL evidence writer and fanout plan;
- send boundary, sender adapter and endpoint resolution;
- client ACK/NACK contract and server receive-loop route proof;
- terminal ACK/NACK/timeout plan;
- live diagnostic send behind explicit flag;
- in-memory outbound delivery terminal/idempotency state;
- optional AOI fanout selector;
- no-apply client main-thread presentation, speaker resolution and presenter
  preflight;
- server in-memory conversation observers;
- no-send late-observer resume plan, packet boundary, send gate, delivery
  registration boundary, dispatch envelope, mutation guard, failure/dead-letter
  guard and commit preflight;
- disabled-by-default Step274 persistence preview bridge that converts the
  commit-preflight state into typed Step273 SQL/procedure statement previews
  while forcing `execute_mysql=off`.

Hard contract: real dialog UI/audio, replay UDP send, durable apply and
`mark_applied` remain disabled until the durable DB receipt path is validated.

## Step273 DB Contract

`server/sql/step273_ai_dialog_intent_delivery_conversation_storage.sql` adds
durable surfaces in `mmo_ai_runtime`:

- `dialog_intent_conversation_sessions`
- `gameplay_outbound_deliveries`
- `dialog_intent_conversation_observers`
- `gameplay_delivery_receipts`
- `gameplay_delivery_dead_letters`
- health/detail views for delivery and conversation observer inspection
- procedures for delivery sent, delivery receipt, timeout and dead-letter
  recording

This migration is the storage prerequisite for future ACK/observation/apply
logic. It has a focused checker and must stay green before executable C++ starts
depending on it. Step274 only previews conversation upsert, delivery sent call,
observer upsert, future receipt call and future dead-letter call; it does not
open MySQL, call `runMysql` or mutate runtime DB state.

## MySQL And Identity

Runtime MMO DB stores sessions, characters, current world state, event journal
and projections. `mmo_content_build` stores parsed static content.
`mmo_ai_runtime` stores AI/perception decisions, action queue state and now the
Step273 dialog-intent delivery/receipt/conversation storage contract.

Durable identity should use character key, world instance, content revision,
world name, persistent/VOB/script IDs, template keys and DB UUIDs. Display names
are labels only.


