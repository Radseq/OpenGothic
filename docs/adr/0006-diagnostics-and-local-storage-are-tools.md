# ADR 0006 — Diagnostics and Local Storage Are Tools

Status: accepted

## Context

JSONL captures and SQLite can aid debugging, replay analysis and migration, but
a local durable path would create a second gameplay authority and insecure
recovery behavior.

## Decision

Production server-bound gameplay uses only typed facade state. JSONL,
filesystem snapshots and SQLite are opt-in diagnostic/offline tooling, disabled
by default and unable to activate gameplay. They must not contain required
credentials or become prerequisites for startup, reconnect or resync.

## Consequences

- Production client code owns no database schema or persistence migration.
- Tooling dependencies remain optional and isolated from normal builds.
- Removing a diagnostic artifact cannot change authoritative behavior.
- Any proposal for offline caching requires a new ADR and must preserve server
  validation and revision semantics.
