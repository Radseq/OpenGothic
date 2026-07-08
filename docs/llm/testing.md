# Testing

Use the smallest validation that covers the touched behavior. Do not run
destructive DB reset unless explicitly requested.

## Build Client

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target Gothic2Notr -j
```

## Build Server Tools

```bash
cmake -S server/cpp -B build/mmo_cpp_server -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/mmo_cpp_server -j
```

Verified 2026-07-08: full `server/cpp` build passed after adding Step232-242
targets to CMake.

## Export Runtime Read-Model

```bash
tools/export_content_build_runtime_read_model.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --output /tmp/opengothic_step241_runtime_read_model.json \
  --sql-output /tmp/opengothic_step241_register_runtime_export.sql
```

Verified summary:

```text
world_zen=24917 waypoint_edges=3202 npc_templates=730 item_templates=835
routines=1186 perception_bindings=38 dialog_infos=4139 dialog_outputs=20826
```

## Read-Only Probes

```bash
build/mmo_cpp_server/mmo_runtime_read_model_probe \
  /tmp/opengothic_step241_runtime_read_model.json

build/mmo_cpp_server/mmo_world_instance_content_cache_probe \
  /tmp/opengothic_step241_runtime_read_model.json \
  gothic2-notr-steam-local newworld newworld

build/mmo_cpp_server/mmo_npc_perception_policy_probe \
  /tmp/opengothic_step241_runtime_read_model.json \
  gothic2-notr-steam-local newworld newworld

build/mmo_cpp_server/mmo_npc_perception_record_probe \
  mysql://user:pass@127.0.0.1:3306/gothic_mmo_ch1_clean \
  /tmp/opengothic_step241_runtime_read_model.json \
  gothic2-notr-steam-local newworld newworld \
  --synthetic --dry-run \
  --world-instance-uuid 00000000-0000-0000-0000-000000000000

build/mmo_cpp_server/mmo_udp_server \
  --no-direct-db \
  --runtime-read-model-path /tmp/opengothic_step241_runtime_read_model.json \
  --content-revision-key gothic2-notr-steam-local \
  --world-instance-key newworld \
  --world-name newworld \
  --startup-check-only
```

Expected current result: `status=ready` or `startup_check=ok`. Runtime probe
still reports the known Step226 warnings.

## MySQL Checks

Requires local MySQL access through `$MYSQL_URL`.

```bash
tools/check_mmo_step212_ai_runtime_perception_database.py \
  --url "$MYSQL_URL" \
  --output /tmp/opengothic_step241_check_step212.json

tools/check_mmo_step213_ai_runtime_action_dispatch.py \
  --url "$MYSQL_URL" \
  --output /tmp/opengothic_step241_check_step213.json

build/mmo_cpp_server/mmo_npc_perception_action_queue_probe "$MYSQL_URL"

build/mmo_cpp_server/mmo_npc_perception_action_dispatcher_probe "$MYSQL_URL"
```

Verified 2026-07-08:

- Step212 status: `passed`;
- Step213 status: `passed`;
- action queue read-only probe worked;
- dispatcher read-only probe worked and reported `live_dispatch_executed=false`.

## Manual AI Tick Guard

```bash
build/mmo_cpp_server/mmo_world_instance_ai_tick_probe \
  "$MYSQL_URL" \
  /tmp/opengothic_step241_runtime_read_model.json \
  gothic2-notr-steam-local newworld newworld \
  --max-npcs 8 \
  --max-players 4 \
  --max-records 1 \
  --max-distance 1000000
```

Current live DB result is expected to reject recording because NPC identity is
incomplete:

```text
status=no_actor_pair
accepted_npcs=0
skipped_missing_npc_instance=1
recorded_count=0
```

## UDP Startup AI Dry-Run

```bash
build/mmo_cpp_server/mmo_udp_server \
  --no-direct-db \
  --mysql-url "$MYSQL_URL" \
  --runtime-read-model-path /tmp/opengothic_step241_runtime_read_model.json \
  --content-revision-key gothic2-notr-steam-local \
  --world-instance-key newworld \
  --world-name newworld \
  --world-instance-ai-startup-dry-run \
  --world-instance-ai-max-npcs 8 \
  --world-instance-ai-max-players 4 \
  --world-instance-ai-max-distance 1000000 \
  --startup-check-only
```

Verified 2026-07-08: `startup_check=ok`,
`world_instance_ai_startup_dry_run=accepted`, `write_executed=0`.

## Step236-241 Dispatcher Chain

Create one synthetic queued action:

```bash
build/mmo_cpp_server/mmo_npc_perception_record_probe \
  "$MYSQL_URL" \
  /tmp/opengothic_step241_runtime_read_model.json \
  gothic2-notr-steam-local newworld newworld \
  --synthetic \
  --max-records 1 \
  --world-instance-uuid <world-instance-uuid> \
  --server-tick 241 \
  --target-key PC_HERO_STEP241
```

Validate and clean it up:

```bash
build/mmo_cpp_server/mmo_npc_perception_action_dispatcher_probe \
  "$MYSQL_URL" \
  --claim-one \
  --require-typed-effect \
  --preview-dialog-intent \
  --require-preview \
  --emit-preview-diagnostic-packet \
  --require-preview-diagnostic-packet \
  --encode-preview-diagnostic-packet \
  --require-preview-diagnostic-encoding \
  --write-preview-evidence-jsonl /tmp/opengothic_step241_dialog_intent_preview_evidence.jsonl \
  --require-preview-evidence \
  --skip-after-claim \
  --worker-id step241-local-evidence-chain \
  --i-understand-this-mutates-db \
  --i-understand-this-writes-evidence
```

Verified 2026-07-08:

- status: `claimed_dialog_intent_durable_evidence_written_and_skipped`;
- typed effect, preview, diagnostic packet, binary encoding and JSONL evidence
  all passed;
- no live dispatch/send/UI/audio/apply executed;
- final queue probe reported `pending_count=0`.

## Known Step242 Guard

The Step242 fanout-plan path rejects synthetic actions without target
`session_uuid` and `character_uuid`:

```text
status=claimed_dialog_intent_client_fanout_plan_failed
issues=["missing_target_session_uuid","missing_target_character_uuid"]
```

This remains the expected result for plain synthetic rows. Use
`--target-from-runtime-player` when the test needs a session-bound target.

## Step242 Session-Bound Fanout Plan

`mmo_npc_perception_record_probe` supports `--target-from-runtime-player` for
synthetic NPC decisions that use a real active target session/character from DB.

```bash
build/mmo_cpp_server/mmo_npc_perception_record_probe \
  "$MYSQL_URL" \
  /tmp/opengothic_step241_runtime_read_model.json \
  gothic2-notr-steam-local newworld newworld \
  --synthetic \
  --target-from-runtime-player \
  --max-records 1 \
  --target-distance 100 \
  --server-tick 243001
```

Then run the Step236-Step242 dispatcher chain with `--plan-preview-client-fanout`
and `--require-preview-client-fanout-plan`.

Verified 2026-07-08:

- status: `claimed_dialog_intent_client_fanout_planned_and_skipped`;
- fanout plan `built=true`, `planned_datagrams=1`, `fits_single_datagram=true`;
- target `session_uuid` and `character_uuid` were present;
- all send/fan-out/UI/audio/apply booleans remained `false`.
