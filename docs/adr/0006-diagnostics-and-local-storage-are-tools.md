# ADR 0006 — Diagnostics and Local Storage Are Tools

## Status

Accepted.

## Context

JSONL captures and SQLite can aid debugging, replay analysis and migration, but
a local durable path in the full client would create a second gameplay
authority and unsafe recovery behavior.

## Decision

Production server-bound gameplay uses only typed facade state. JSONL,
filesystem snapshots and SQLite are opt-in diagnostic/offline tooling,
disabled by default and unable to activate gameplay.

Diagnostic/local artifacts:

- must not be required for startup, reconnect or resync;
- must not contain required credentials;
- must not become a command or bootstrap compatibility channel;
- may be removed without changing authoritative behavior.

## Rationale

Diagnostic storage has different trust, lifecycle and consistency guarantees
than server persistence. Keeping it optional prevents debug convenience from
becoming production recovery truth.

## Alternatives

- **Use client SQLite as an offline gameplay cache:** rejected because cached
  state cannot become authoritative after reconnect.
- **Use JSON/file snapshots as fallback bootstrap:** rejected because it
  bypasses route, revision and server admission rules.
- **Remove all diagnostics:** rejected because bounded, opt-in telemetry and
  offline analysis remain useful.

## Scope and consequences

- Production client code owns no MMO database schema or persistence migration.
- Tooling dependencies remain optional and isolated from normal builds.
- A new offline-cache proposal requires a superseding ADR and must preserve
  server validation, route identity and revision semantics.

## Evidence

- `src/client/CMakeLists.txt` keeps
  `OPENGOTHIC_MMO_ENABLE_SQLITE_TOOLING` off by default and compiles the SQLite
  source only when explicitly enabled.
- `tools/check_client_mmo_sandbox_boundary.py` rejects filesystem/JSON/SQLite
  gameplay side channels and retired compatibility contracts.
- `src/client/game/game/mmoclientdiagnostics.cpp` emits diagnostic JSON rather
  than an authoritative runtime command or restore contract.
- `src/client/docs/llm/current-state.md` records the remaining unreachable
  legacy restore path as removal debt, not supported behavior.
