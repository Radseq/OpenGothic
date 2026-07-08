# AGENTS.md

## Project Summary

OpenGothic is a C++23 Gothic/Gothic II NotR engine fork. The active workstream
turns Gothic II NotR into a server-authoritative MMO while preserving native
single-player behavior unless explicit MMO flags are used.

Main goals:
- keep single-player behavior unchanged without MMO flags;
- move gameplay truth from client/save files to server authority;
- keep content parsing separate from runtime state;
- make changes small, explicit and compatible with existing code style.

## Tech Stack

- C++23.
- ZenKit for Gothic content formats.
- Standalone ASIO for MMO UDP transport.
- MySQL for MMO server authority schemas.
- Python tools for migration, reports and validation.

## Requirements

- Use C++23.
- Use `constexpr` where it makes sense and improves clarity or compile-time
  guarantees.
- Prioritize maximum performance without sacrificing correctness.
- Write safe, readable and production-quality code.
- Build a solid foundation for a future Gothic-based MMO.
- Write like a senior developer: relatively small functions, clear separation
  of concerns, modular design and code open for extension but closed for
  unnecessary modification.
- Old client behavior must keep working unchanged.
- New server/MMO behavior must be used only when the client is started with an
  explicit server/MMO parameter or flag.

## Read First

Default context lives in `docs/llm/README.md`.

Read it before changing MMO/server/content code. The old numbered
`docs/llm/ai/*.md` files are history and should be opened only when a specific
step or regression needs archaeology.

## Important Directories

- `game/` - client/game/engine code.
- `game/game/` - gameplay session, MMO hooks, semantic events and restore code.
- `game/graphics/`, `shader/` - rendering.
- `server/cpp/` - C++ MMO server and content/read-model tools.
- `server/sql/` - MySQL schema/procedure surfaces.
- `tools/` - Python wrappers, reports and validation entrypoints.
- `tools/bootstrap/` - implementation behind wrapper tools.
- `tools/validation/` - focused validation scripts.
- `docs/llm/` - current LLM context map.
- `docs/llm/ai/` - step archive; not default context.
- `runtime/` - generated local artifacts; do not treat as source.

## Build

Client:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target Gothic2Notr -j
```

Server/content tools:

```bash
cmake -S server/cpp -B build/mmo_cpp_server -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/mmo_cpp_server --target mmo_udp_server -j
```

Useful focused targets:

```bash
cmake --build build/mmo_cpp_server --target mmo_content_build_importer -j
cmake --build build/mmo_cpp_server --target mmo_vdf_world_zen_probe -j
cmake --build build/mmo_cpp_server --target mmo_runtime_read_model_probe -j
```

## Test And Validate

Prefer the narrowest relevant validation:

```bash
python3 -m py_compile tools/bootstrap/export_content_build_runtime_read_model.py tools/export_content_build_runtime_read_model.py
build/mmo_cpp_server/mmo_runtime_read_model_probe /tmp/opengothic_step225_runtime_read_model.json
```

Database tools require a real local MySQL URL. Do not run destructive DB reset
commands unless the user clearly asked for that operation.

## Coding Rules

- Prefer existing local patterns over new abstractions.
- Use `constexpr` where it is useful, not as decoration.
- Keep hot paths allocation-conscious and predictable.
- Keep ownership explicit; use RAII and value types where practical.
- Prefer `std::string_view`, references and spans for non-owning views.
- Keep functions relatively small and focused.
- Separate responsibilities between parsing, validation, storage, runtime cache
  and gameplay decisions.
- Prefer modular extension points over modifying stable behavior.
- Avoid raw owning pointers and hidden global mutable state.
- Do not make hot-path gameplay depend on JSON when typed packets/columns exist.
- Do not silently change serialized names, DB procedures, packet fields or
  content read-model schemas.
- Keep server modules focused; do not grow `mmo_udp_server.cpp` when a focused
  module is appropriate.
- Python wrapper tools in `tools/` should stay thin; implementation belongs in
  `tools/bootstrap/` or `tools/validation/`.

## Authority Rules

- The client is not source of truth for MMO world, NPCs, dialogs or perception.
- No accepted gameplay mutation without server validation and one durable event.
- `mmo_content_build` is build-time content, not runtime gameplay state.
- `mmo_ai_runtime` is runtime AI/perception decision state, not parser output.
- Server script/NPC logic should run once per `world_instance`, not once per
  player client.
- `mmo_server_action_outbox` is debug/fallback only.

## Avoid

- Do not remove native save/single-player compatibility.
- Do not persist live pointers, animation frames, transient AI queues, render
  state, audio state, camera/input/focus state.
- Do not use display names as stable identity.
- Do not commit generated `runtime/` artifacts as source context.
