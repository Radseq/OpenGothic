# Testing

Use the smallest validation that covers the touched behavior. Do not run
destructive DB reset unless the user clearly requested it.

## Build Client

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target Gothic2Notr -j
```

## Build Server Tools

```bash
cmake -S server/cpp -B build/mmo_cpp_server -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/mmo_cpp_server --target mmo_udp_server -j
```

Focused targets:

```bash
cmake --build build/mmo_cpp_server --target mmo_udp_server -j
cmake --build build/mmo_cpp_server --target mmo_content_build_importer -j
cmake --build build/mmo_cpp_server --target mmo_vdf_world_zen_probe -j
cmake --build build/mmo_cpp_server --target mmo_runtime_read_model_probe -j
cmake --build build/mmo_cpp_server --target mmo_world_instance_content_cache_probe -j
cmake --build build/mmo_cpp_server --target mmo_npc_perception_policy_probe -j
cmake --build build/mmo_cpp_server --target mmo_npc_perception_record_probe -j
```

## Content Build Validation

Discover sources:

```bash
tools/discover_gothic_content_sources.py \
  --gothic-root "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  --output runtime/content_build/gothic_content_sources.json \
  --run-script runtime/content_build/run_content_build_importer.sh
```

Probe/extract world ZEN from VDF/MOD:

```bash
cmake --build build/mmo_cpp_server --target mmo_vdf_world_zen_probe -j
tools/probe_gothic_world_zen_archives.py \
  --gothic-root "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  --world-name newworld.zen \
  --extract
```

Import content build snapshot to DB:

```bash
MMO_CONTENT_BUILD_DB_MODE=apply runtime/content_build/run_content_build_importer.sh
```

Check DB:

```bash
tools/check_mmo_step211_content_build_database.py \
  --url "$MYSQL_URL" \
  --expect-zen \
  --expect-daedalus \
  --expect-dialog-outputs \
  --output runtime/step211_content_build_database/check_after_zen_import.json
```

## Runtime Read-Model Validation

Export:

```bash
tools/export_content_build_runtime_read_model.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --output /tmp/opengothic_runtime_read_model.json \
  --sql-output /tmp/opengothic_register_runtime_export.sql
```

C++ probe:

```bash
build/mmo_cpp_server/mmo_runtime_read_model_probe \
  /tmp/opengothic_runtime_read_model.json
```

Expected current status:

```text
status=ready
world_zen_entity_count=24917
world_zen_entity_by_key=24917
waypoint_edge_by_route=3202
npc_template_by_instance=730
item_template_by_instance=835
routine_by_npc_instance=0
routine_by_symbol=1186
perception_binding_by_kind=6
perception_binding_by_owner=0
dialog_info_by_symbol=4139
dialog_output_by_name=20826
```

Current expected warnings:

```text
routines without npc_instance=1186
perception bindings without owner_symbol=38
```

## World Instance Content Cache Validation

Positive probe:

```bash
build/mmo_cpp_server/mmo_world_instance_content_cache_probe \
  /tmp/opengothic_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld
```

Expected current status:

```text
status=ready
world_zen_entities_in_world=24917
waypoint_edges_in_world=3202
first_world_zen_entity_in_world=true
first_waypoint_edge_in_world=true
first_routine_by_npc_instance=false
first_perception_binding_by_owner=false
```

Negative revision check:

```bash
build/mmo_cpp_server/mmo_world_instance_content_cache_probe \
  /tmp/opengothic_runtime_read_model.json \
  wrong-revision \
  newworld \
  newworld
```

Expected result: `status=error` with a content revision mismatch.

## Server Startup Content Cache Validation

```bash
build/mmo_cpp_server/mmo_udp_server \
  --no-direct-db \
  --runtime-read-model-path /tmp/opengothic_runtime_read_model.json \
  --content-revision-key gothic2-notr-steam-local \
  --world-instance-key newworld \
  --world-name newworld \
  --startup-check-only
```

Expected current status:

```text
world_instance_content_cache=ready
world_zen_entities_in_world=24917
waypoint_edges_in_world=3202
```

Negative revision check: replace `gothic2-notr-steam-local` with
`wrong-revision`. Expected result: startup exits with a content revision
mismatch error.

## NPC Perception Policy Validation

Positive probe:

```bash
build/mmo_cpp_server/mmo_npc_perception_policy_probe \
  /tmp/opengothic_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld
```

Expected current status:

```text
status=ready
evaluated_pairs=1
decisions=1
decision_kind=greet_player
rule_key=B_ASSESSPLAYER
```

Distance rejection:

```bash
build/mmo_cpp_server/mmo_npc_perception_policy_probe \
  /tmp/opengothic_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld \
  --target-distance 5000 \
  --max-distance 100
```

Expected result: `status=no_decision`, `distance_pairs_skipped=1`.

## AI Runtime Perception Recording Validation

Build:

```bash
cmake --build build/mmo_cpp_server --target mmo_npc_perception_record_probe -j
```

Dry-run without DB mutation:

```bash
build/mmo_cpp_server/mmo_npc_perception_record_probe \
  "$MYSQL_URL" \
  /tmp/opengothic_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld \
  --dry-run \
  --max-npcs 8 \
  --max-players 4 \
  --max-records 1
```

Synthetic dry-run without DB mutation:

```bash
build/mmo_cpp_server/mmo_npc_perception_record_probe \
  mysql://user:pass@127.0.0.1:3306/gothic_mmo_ch1_clean \
  /tmp/opengothic_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld \
  --synthetic \
  --dry-run \
  --world-instance-uuid 00000000-0000-0000-0000-000000000000
```

Expected current synthetic dry-run:

```text
status=ready_dry_run
decisions=1
decision_kind=greet_player
recorded_count=0
```

Controlled DB record probe:

```bash
build/mmo_cpp_server/mmo_npc_perception_record_probe \
  "$MYSQL_URL" \
  /tmp/opengothic_runtime_read_model.json \
  gothic2-notr-steam-local \
  newworld \
  newworld \
  --synthetic \
  --max-records 1 \
  --world-instance-uuid <runtime-world-instance-uuid>
```

Expected result: `recorded_count=1` and `decision_status` is `queued`,
`accepted`, `cooldown`, `blocked` or `noop` according to `mmo_ai_runtime`.
Current verified Step230 synthetic record returned `decision_status=queued`.

## Server/Client Smoke Loop

Server:

```bash
./build/mmo_cpp_server/mmo_udp_server \
  --bind 127.0.0.1:29777 \
  --mysql-url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO
```

Client:

```bash
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-client-server 127.0.0.1:29777 \
  -mmo-action-session-key local-dev-PC_HERO_TEST
```

Expected evidence:

- server prints `bootstrap_ack accepted=1 ready=1`;
- server prints `bootstrap_snapshot_sent`;
- client writes `runtime/mmo_server_bootstrap_snapshot.json`;
- accepted direct DB actions increase;
- `enqueued=0` unless `--enqueue-outbox` was explicitly used.

## Existing DB Apply

For an existing DB, prefer:

```bash
python3 tools/apply_current_mmo_db_state.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo_ch1_clean" \
  --output runtime/current_mmo_db_state/apply.json
```

## Destructive Clean Rebuild

Only when explicitly requested:

```bash
python3 tools/run_mmo_step55_clean_mysql_from_pre_xardas.py \
  --sqlite runtime/g2notr_ch1_pre_xardas.sqlite \
  --mysql-url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo_ch1_clean" \
  --i-understand-this-drops-database
```
