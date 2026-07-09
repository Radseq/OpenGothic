# Testing

Use the smallest validation that covers the touched behavior. Do not run
destructive DB reset unless explicitly requested.

## Build Client

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target Gothic2Notr -j
```

Verified 2026-07-09 after Step274 ZIP: `Gothic2Notr` target completed with
`ninja: no work to do`.

## Build Server

```bash
cmake -S server/cpp -B build/mmo_cpp_server -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/mmo_cpp_server -j
```

Verified 2026-07-09 after Step274 ZIP: full `server/cpp` build passed.

## Runtime Read-Model

```bash
tools/export_content_build_runtime_read_model.py \
  --snapshot runtime/content_build/parser_snapshot.json \
  --output /tmp/opengothic_runtime_read_model.json \
  --sql-output /tmp/opengothic_register_runtime_export.sql

build/mmo_cpp_server/mmo_runtime_read_model_probe \
  /tmp/opengothic_runtime_read_model.json
```

Expected current counts:

```text
world_zen=24917 waypoint_edges=3202 npc_templates=730 item_templates=835
routines=1186 perception_bindings=38 dialog_infos=4139 dialog_outputs=20826
```

## Focused Server Probes

```bash
build/mmo_cpp_server/mmo_world_instance_content_cache_probe \
  /tmp/opengothic_runtime_read_model.json gothic2-notr-steam-local newworld newworld

build/mmo_cpp_server/mmo_npc_perception_policy_probe \
  /tmp/opengothic_runtime_read_model.json gothic2-notr-steam-local newworld newworld

build/mmo_cpp_server/mmo_udp_server \
  --no-direct-db \
  --runtime-read-model-path /tmp/opengothic_runtime_read_model.json \
  --content-revision-key gothic2-notr-steam-local \
  --world-instance-key newworld \
  --world-name newworld \
  --startup-check-only
```

Expected result: `status=ready` or `startup_check=ok`.


## Focused Step274 C++ Preview Checks

Step274 is C++ only and must not open MySQL. Use a focused syntax/test pass for
the preview bridge plus the UDP server integration.

```bash
g++ -std=c++23 -I. -Igame/game \
  -fsyntax-only server/cpp/mmo_ai_dialog_intent_delivery_persistence_bridge.h

# Full project builds should use the real thirdparty Asio include. In stripped
# LLM snapshots a temporary local Asio stub is acceptable for syntax-only checks,
# but it must not be committed or shipped.
g++ -std=c++23 -Ithirdparty/asio/include -I. -Igame/game \
  -fsyntax-only server/cpp/mmo_udp_server.cpp
```

Expected preview runtime evidence: `execute_mysql=0`, `mutated_db=0` and no
`runMysql` call from the Step274 path.

## MySQL Checks

Requires local MySQL access through `$MYSQL_URL`.

```bash
tools/check_mmo_step212_ai_runtime_perception_database.py \
  --url "$MYSQL_URL" \
  --output /tmp/opengothic_check_step212.json

tools/check_mmo_step213_ai_runtime_action_dispatch.py \
  --url "$MYSQL_URL" \
  --output /tmp/opengothic_check_step213.json

tools/check_mmo_step273_ai_dialog_intent_delivery_conversation_storage.py \
  --url "$MYSQL_URL" \
  --output /tmp/opengothic_check_step273.json
```

Step273 source migration can be applied manually only when the target DB is
confirmed:

```bash
mysql "$MYSQL_URL" < server/sql/step273_ai_dialog_intent_delivery_conversation_storage.sql
```

After applying Step273, run the checker above to confirm tables, views,
procedures and the schema marker.

Verified 2026-07-09: Step212, Step213 and Step273 checkers passed against the
local `$MYSQL_URL` target.

## LLM DB Ledger Tools

The ledger remains a planned-only historical record for the paused DB period.

```bash
python3 -m py_compile \
  tools/validation/check_llm_db_changes_ledger.py \
  tools/validation/export_llm_db_changes_plan.py \
  tools/check_llm_db_changes_ledger.py \
  tools/export_llm_db_changes_plan.py

tools/check_llm_db_changes_ledger.py --strict \
  --output /tmp/opengothic_llm_db_ledger_check.json

tools/export_llm_db_changes_plan.py --strict \
  --output /tmp/opengothic_llm_db_plan.json
```

Expected: both tools pass and report no DB mutation.

