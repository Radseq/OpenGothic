# Step 38 fixture schema-order fix

Date: 2026-06-28

## Problem

The first Step38 combat/resource JSONL proof was good, but the MySQL E2E run with
`--prepare-dev-fixture` failed before preparing any fixture rows:

```text
ERROR 1054 (42S22): Unknown column 'ss.updated_at' in 'order clause'
```

The local MySQL Step30 schema has `server_sessions.login_at` and
`server_sessions.last_seen_at`, but not `server_sessions.updated_at`. The failing
query was only a dev-fixture session lookup; it was not a game hook or stored
procedure failure.

## Fix

`tools/prepare_mmo_step38_dev_fixture.py` now inspects
`information_schema.columns` and chooses an existing order column:

- `server_sessions`: `last_seen_at`, `login_at`, `logout_at`, `created_at`, then
  `updated_at` if present on a future schema.
- `mmo_server_action_outbox`: `requested_at`, `updated_at`, `applied_at`, then
  `failed_at`.

The chosen columns are emitted into the fixture manifest under `session` so the
run artifact shows exactly which schema path was used.

## Meaning

This keeps the fixture compatible with the actual MySQL schema while preserving
its dev-only role. It still must not be used as parity proof. It exists only to
make a captured Step38 combat/resource JSONL replay executable when runtime
SQLite/OpenGothic state and imported MySQL projection are out of sync.
