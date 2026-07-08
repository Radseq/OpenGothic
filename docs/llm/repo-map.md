# Repository Map

Inspect source files before editing. Read only the section that matches the
task.

## Root

- `CMakeLists.txt` - main `Gothic2Notr` client/game target.
- `AGENTS.md` - agent rules for future Codex/LLM work.
- `.llmignore` - context ignore list.
- `README.md` - upstream/user-facing project information.
- `docs/llm/` - compact LLM context and step archive.

## Client/Game

- `game/commandline.*` - runtime flags, including MMO flags.
- `game/mainwindow.*` - app startup and graphics API creation.
- `game/game/gamesession.*` - session lifecycle, tick, snapshot restore.
- `game/game/mmosemanticevents.*` - semantic action definitions.
- `game/game/mmosemanticactionsink.*` - ASIO UDP client path.
- `game/game/mmosemantichooks.*` - gameplay hooks emitting MMO actions.
- `game/game/mmorestoresnapshot.h` - bootstrap snapshot parsing/apply structs.
- `game/world/objects/` - NPC/item/interactive behavior.

Rules:

- Client remains presentation/input/prediction in MMO mode.
- New server behavior must be gated by explicit flags.

## `server/cpp/`

C++ MMO server and content tooling.

Core:

- `mmo_udp_server.cpp` - C++ UDP server and startup flags.
- `mmo_udp_server_*.inl` - extracted server partitions.
- `mmo_server_types.h` - shared options/types.
- `mmo_server_persistence.*` - MySQL helper/procedure bridge.
- `mmo_server_world_clock.h` - shared world clock snapshot query helper.

Content/read-model:

- `mmo_content_build_loader.*`
- `mmo_content_build_importer.cpp`
- `mmo_runtime_read_model_loader.*`
- `mmo_runtime_read_model_probe.cpp`
- `mmo_world_instance_content_cache.*`
- `mmo_world_instance_content_cache_probe.cpp`
- `mmo_vdf_world_zen_probe.cpp`

NPC perception and AI runtime:

- `mmo_npc_perception_policy.*`
- `mmo_npc_perception_policy_probe.cpp`
- `mmo_npc_perception_runtime_source.*`
- `mmo_ai_runtime_persistence.*`
- `mmo_npc_perception_record_probe.cpp`
- `mmo_world_instance_ai_tick.*`
- `mmo_world_instance_ai_tick_evidence.*`
- `mmo_world_instance_ai_scheduler_boundary.*`
- `mmo_world_instance_ai_tick_probe.cpp`

Action queue / dispatcher preview:

- `mmo_npc_perception_action_queue_probe.cpp`
- `mmo_npc_perception_action_dispatcher_boundary.*`
- `mmo_npc_perception_action_dispatcher_probe.cpp`
- `mmo_npc_perception_effect_descriptor.*`
- `mmo_npc_perception_dialog_intent_preview.*`
- `mmo_npc_perception_dialog_intent_diagnostic_packet.*`
- `mmo_npc_perception_dialog_intent_diagnostic_encoder.*`
- `mmo_npc_perception_dialog_intent_durable_evidence.*`
- `mmo_npc_perception_dialog_intent_fanout_plan.*`

Rules:

- Prefer focused modules over growing `mmo_udp_server.cpp`.
- Keep server hot paths typed; JSON is bridge/debug unless explicitly a read
  model artifact.

Build:

- `server/cpp/CMakeLists.txt` - standalone server/content-tool targets.

## SQL

- `server/sql/step211_content_build_database.sql` - `mmo_content_build`.
- `server/sql/step212_ai_runtime_perception_database.sql` - `mmo_ai_runtime`.
- `server/sql/step213_ai_runtime_action_dispatch_contracts.sql` - AI action
  dispatch queue.

Rules:

- Step SQL files are migration history/contracts.
- Do not rename procedures/columns without migration and validation updates.

## Tools

- `tools/*.py` - thin wrappers.
- `tools/bootstrap/*.py` - apply/import/report flows.
- `tools/validation/*.py` - focused checks.

Current useful tools:

- `tools/export_content_build_runtime_read_model.py`
- `tools/check_mmo_step212_ai_runtime_perception_database.py`
- `tools/check_mmo_step213_ai_runtime_action_dispatch.py`

## Runtime

Generated local artifacts: parser snapshots, reports, bootstrap snapshots,
evidence JSONL. Do not treat `runtime/` as source.
