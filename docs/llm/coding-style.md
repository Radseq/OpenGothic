# Coding Style

Use this file for project-specific implementation preferences. General
architecture decisions are in `decisions.md`.

## C++

MUST:

- Use C++23.
- Preserve existing naming/formatting patterns in nearby files.
- Keep changes localized to the current domain.
- Keep functions focused and responsibilities separated.
- Prefer RAII and explicit ownership.
- Prefer value semantics where practical.
- Use `std::string_view`, references or spans for non-owning inputs.
- Keep API boundaries explicit about ownership/lifetime.
- Preserve old client behavior unless explicit MMO/server flags select new
  behavior.

SHOULD:

- Use `constexpr` when it improves clarity, safety or compile-time guarantees.
- Use small structs for contracts.
- Keep validation and parsing deterministic.
- Keep hot-path logic typed and allocation-conscious.
- Mark functions `noexcept` only when it is actually correct.
- Extract focused modules instead of growing monolithic files.
- Build stable foundations for future MMO systems instead of short-lived hacks.

AVOID:

- Raw owning pointers.
- Hidden global mutable state.
- Broad refactors mixed with feature work.
- New macro systems.
- Catch-all behavior that hides protocol/data errors.

## Server

MUST:

- Validate before mutating DB.
- Keep one durable event/projection path per accepted gameplay mutation.
- Keep content-build data separate from runtime gameplay state.
- Activate server/MMO behavior only through explicit flags/parameters.

SHOULD:

- Prefer typed packets/columns/procedures for hot authority paths.
- Treat JSON as bridge/debug/read-model artifact unless a contract says
  otherwise.

## Python Tools

MUST:

- Keep wrappers in `tools/` thin.
- Put implementation in `tools/bootstrap/` or `tools/validation/`.
- Make generated SQL/report paths explicit.
- Avoid implicit DB apply for dangerous/destructive operations.

SHOULD:

- Emit machine-readable JSON reports.
- Redact credentials in printed commands.
- Use deterministic ordering for generated artifacts.

## SQL

MUST:

- Preserve procedure/table/view names unless adding migration/compat updates.
- Keep schema changes idempotent where possible.
- Keep binary UUID handling explicit; GUI tools may show `BINARY(16)` as BLOB.

AVOID:

- Display-name identity.
- JSON-only hot gameplay state.
- Dropping/recreating data outside explicit destructive reset tools.
