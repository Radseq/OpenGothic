# Coding Style

## General

- Prefer clear, explicit code over clever abstractions.
- Keep changes small and locally verifiable.
- Preserve existing style in touched files unless it is unsafe.
- Avoid broad rewrites unrelated to the task.
- Use names that encode authority and ownership, e.g. `Server`, `Runtime`, `ContentBuild`, `WorldInstance`.

## C++

Project direction: modern C++ with performance awareness.

Guidelines:

- Prefer value types, RAII and explicit ownership.
- Avoid global mutable state unless existing architecture already requires it.
- Use `std::string_view` for non-owning parse/lookups when lifetimes are obvious.
- Do not store `string_view` into containers unless the backing storage lifetime is guaranteed and documented.
- Validate parsed external data before indexing it.
- Make missing required data fail loudly in probes/checks.
- Use deterministic ordering for generated reports/check output.
- Keep hot gameplay paths typed and indexed; avoid repeated JSON parsing.
- Keep client MMO hooks lightweight.

## Error Handling

- Return explicit status/result where expected by existing code.
- Include enough context in diagnostics to identify content revision/world/entity/packet.
- Do not silently ignore malformed server-authoritative data.
- Do not crash native single-player paths because MMO-only state is unavailable.

## SQL

- Prefer explicit columns and indexes over opaque JSON for runtime state.
- Use JSON for diagnostics/raw evidence/generated artifacts where appropriate.
- Keep schema changes idempotent where tools expect repeated apply.
- Avoid unnecessary views/procedures when application-side typed logic is clearer.
- If a view/procedure is required, add or update a focused check tool.

## Python/Shell Tools

- Use deterministic output paths under `runtime/` or explicit `--output`.
- Fail non-zero on real validation failure.
- Print concise summaries and expected warnings separately from errors.
- Avoid destructive DB operations unless guarded by explicit flags such as `--i-understand-this-drops-database`.
- Keep credentials configurable through arguments/env; do not hardcode personal paths except in examples.

## Docs

- Update `docs/llm` only for durable context.
- Do not paste full logs.
- Prefer short sections, lists and exact command snippets.
- When a file becomes stale, update `current-state.md` and `next-work.md` first.
