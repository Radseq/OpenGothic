# Repository Map

Read the section that matches the task. Inspect source files before editing.

## Root

- `CMakeLists.txt` - main `Gothic2Notr` client/game target.
- `AGENTS.md` - agent rules for future Codex/LLM work.
- `.llmignore` - context ignore list.
- `README.md` - upstream/user-facing project information.

## `game/`

Client/game/engine code.

Key areas:

- `game/main.cpp`, `game/mainwindow.*` - app startup and graphics API creation.
- `game/commandline.*` - runtime flags, including MMO flags.
- `game/game/gamesession.*` - new/load session, tick, snapshot restore,
  movement checkpoint cadence.
- `game/game/mmosemanticevents.*` - semantic action definitions.
- `game/game/mmosemanticactionsink.*` - async action sink and ASIO UDP client.
- `game/game/mmosemantichooks.*` - gameplay hooks that emit MMO actions.
- `game/game/mmorestoresnapshot.h` - bootstrap snapshot parsing/apply structs.
- `game/game/mmoruntimesqlite.*` - local SQLite capture/restore support.
- `game/world/objects/` - NPC/item/interactive object behavior.
- `game/graphics/`, `shader/` - rendering and shaders.

Rules:

- Keep client as presentation/input/prediction for MMO work, not authority.
- New server/MMO behavior must stay gated by explicit client parameters/flags.

## `server/cpp/`

C++ MMO server and content tooling.

Key files:

- `mmo_udp_server.cpp` - older monolithic C++ UDP server.
- `mmo_udp_server_*.inl` - extracted implementation partitions.
- `mmo_server_persistence.*` - direct DB persistence/query helpers.
- `mmo_server_identity.h` - stable entity/key helpers.
- `mmo_server_types.h` - shared server option/readiness/types.
- `mmo_server_snapshot_limits.h` - snapshot/chunk limits.
- `mmo_content_build_loader.*` - C++ ZenKit parser snapshot writer.
- `mmo_content_build_importer.cpp` - CLI for parser snapshot generation.
- `mmo_vdf_world_zen_probe.cpp` - VDF/MOD world ZEN probe/extractor.
- `mmo_runtime_read_model_loader.*` - Step226 C++ read-model inspection and
  read-only runtime index materialization.
- `mmo_runtime_read_model_probe.cpp` - CLI probe for runtime read-model JSON,
  index counts and deterministic lookup checks.
- `mmo_world_instance_content_cache.*` - Step227 read-only cache binding a
  runtime read-model to a content revision and `world_instance`.
- `mmo_world_instance_content_cache_probe.cpp` - CLI probe for cache binding,
  cache stats and deterministic cache lookup checks.
- Step228 wires the content cache into `mmo_udp_server` startup via explicit
  server flags and `--startup-check-only`.
- `mmo_npc_perception_policy.*` - Step229 C++ candidate assessment for
  NPC/player perception pairs using the content cache.
- `mmo_npc_perception_policy_probe.cpp` - CLI probe for deterministic
  perception candidate decisions.
- `mmo_npc_perception_runtime_source.*` - Step230 runtime DB actor-source for
  active player/NPC perception inputs.
- `mmo_ai_runtime_persistence.*` - Step230 C++ adapter for
  `mmo_ai_record_npc_perception_decision(...)`.
- `mmo_npc_perception_record_probe.cpp` - Step230 CLI probe for live/synthetic
  perception recording to `mmo_ai_runtime`.
- `mmo_server_world_clock.h` - shared world clock bootstrap query helper used
  by the persistence module.
- `CMakeLists.txt` - standalone server/content-tool targets.

Rules:

- Prefer focused modules over growing `mmo_udp_server.cpp`.
- Keep server hot paths typed; JSON is bridge/debug unless explicitly a read
  model artifact.

## `server/sql/`

MySQL schema/procedure contracts.

Important surfaces:

- MMO runtime authority schemas and migration history.
- `step211_content_build_database.sql` - `mmo_content_build`.
- `step212_ai_runtime_perception_database.sql` - `mmo_ai_runtime`.
- `step213_ai_runtime_action_dispatch_contracts.sql` - perception action queue.
- `step221_content_build_item_templates_compat.sql` - compatibility fix for
  item templates.

Rules:

- Step SQL files are migration history and contracts.
- Do not rename procedures/columns without migration and validation updates.
- Treat JSON columns as raw/audit/debug unless the current contract says
  otherwise.

## `tools/`

Python entrypoints.

Patterns:

- `tools/*.py` wrappers are thin CLI entrypoints.
- `tools/bootstrap/*.py` contains implementation for apply/import/report flows.
- `tools/validation/*.py` contains focused checks.
- `tools/migration/` and `tools/production/` are older/supporting paths.

Current content tools:

- `discover_gothic_content_sources.py`
- `probe_gothic_world_zen_archives.py`
- `import_content_build_snapshot_database.py`
- `export_content_build_runtime_read_model.py`
- `check_mmo_step211_content_build_database.py`

## `docs/llm/`

Current compact LLM context.

- Read root `docs/llm/*.md` selectively.
- `docs/llm/ai/*.md` is step archive. Open only targeted files.

## `runtime/`

Generated local artifacts.

Examples:

- parser snapshots;
- generated SQL previews;
- bootstrap snapshots;
- content build reports;
- validation reports.

Do not treat `runtime/` as source.
