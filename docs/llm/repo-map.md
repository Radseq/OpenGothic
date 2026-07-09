# Repository Map

Inspect source files before editing. Read only the section that matches the
task.

## Root

- `CMakeLists.txt` - main `Gothic2Notr` client/game target.
- `AGENTS.md` - agent rules for future Codex/LLM work.
- `.llmignore` - context ignore list.
- `docs/llm/` - compact LLM context and step archive.

## Client/Game

- `game/commandline.*` - runtime flags, including MMO flags.
- `game/game/gamesession.*` - session lifecycle, tick, snapshot restore and
  MMO client hooks.
- `game/game/mmosemanticevents.*` - semantic action definitions.
- `game/game/mmosemanticactionsink.*` - ASIO UDP client path, fast
  `ClientGameplayAck` and optional `ClientGameplayObservation` receipts.
- `game/game/mmoserverdialogpresentation.h` - no-apply client dialog
  presentation/speaker/preflight structs.
- `game/game/mmosemantichooks.*` - gameplay hooks emitting MMO semantic actions.
- `game/game/mmorestoresnapshot.h` - bootstrap snapshot parsing/apply structs.
- `game/world/objects/` - NPC/item/interactive behavior.

Rules: client remains presentation/input/prediction in MMO mode; new server
behavior must be gated by explicit flags.

## Server C++

Core:

- `server/cpp/mmo_udp_server.cpp` - C++ UDP server and startup flags.
- `server/cpp/mmo_udp_server_*.inl` - extracted server partitions.
- `server/cpp/mmo_server_types.h` - shared options/types.
- `server/cpp/mmo_server_persistence.*` - MySQL helper/procedure bridge.
- `server/cpp/mmo_server_world_clock.h` - shared world clock query helper.
- `server/cpp/mmo_outbound_gameplay_delivery_state.h` - in-memory outbound
  gameplay delivery terminal/idempotency state.
- `server/cpp/mmo_server_gameplay_fanout.h` - runtime target/AOI fanout
  selector.

Content/read-model:

- `mmo_content_build_loader.*`
- `mmo_content_build_importer.cpp`
- `mmo_runtime_read_model_loader.*`
- `mmo_runtime_read_model_probe.cpp`
- `mmo_world_instance_content_cache.*`
- `mmo_world_instance_content_cache_probe.cpp`
- `mmo_vdf_world_zen_probe.cpp`

NPC perception/action chain:

- `mmo_npc_perception_policy.*`
- `mmo_npc_perception_runtime_source.*`
- `mmo_ai_runtime_persistence.*`
- `mmo_world_instance_ai_tick.*`
- `mmo_world_instance_ai_scheduler_boundary.*`
- `mmo_npc_perception_action_dispatcher_boundary.*`
- `mmo_npc_perception_effect_descriptor.*`
- `mmo_npc_perception_dialog_intent_*`

Conversation/resume proof chain:

- `mmo_server_conversation_session_boundary.h`
- `mmo_server_conversation_resume_packet_boundary.h`
- `mmo_server_conversation_resume_send_gate.h`
- `mmo_server_conversation_resume_delivery_registration_boundary.h`
- `mmo_server_conversation_resume_dispatch_envelope.h`
- `mmo_server_conversation_resume_mutation_guard.h`
- `mmo_server_conversation_resume_send_failure_dead_letter_guard.h`
- `mmo_server_conversation_resume_commit_preflight.h`
- `mmo_ai_dialog_intent_delivery_persistence_bridge.h` - disabled-by-default
  Step273 persistence preview bridge; builds typed SQL/procedure plans only.

Rules: prefer focused modules over growing `mmo_udp_server.cpp`; keep hot paths
typed and avoid JSON in gameplay loops unless it is a boundary/debug artifact.

## SQL

- `server/sql/step211_content_build_database.sql` - `mmo_content_build`.
- `server/sql/step212_ai_runtime_perception_database.sql` - `mmo_ai_runtime`.
- `server/sql/step213_ai_runtime_action_dispatch_contracts.sql` - AI action
  dispatch queue.
- `server/sql/step273_ai_dialog_intent_delivery_conversation_storage.sql` -
  durable dialog-intent delivery/receipt/conversation/dead-letter storage.

`docs/llm/llm_db_changes/` is a historical planned-only ledger from the paused
DB period; it is not executable migration source.

## Tools

- `tools/*.py` - thin wrappers.
- `tools/bootstrap/*.py` - apply/import/report flows.
- `tools/validation/*.py` - focused checks and read-only exporters.

Useful tools:

- `tools/export_content_build_runtime_read_model.py`
- `tools/check_mmo_step212_ai_runtime_perception_database.py`
- `tools/check_mmo_step213_ai_runtime_action_dispatch.py`
- `tools/check_mmo_step273_ai_dialog_intent_delivery_conversation_storage.py`
- `tools/check_llm_db_changes_ledger.py`
- `tools/export_llm_db_changes_plan.py`

## Runtime

Generated local artifacts live under `runtime/`; do not treat them as source.


