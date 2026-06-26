# Runtime DB Validation

Files: `tools/check_runtime_sqlite.py`, `tools/audit_runtime_sqlite.py`.

- `check_runtime_sqlite.py` is an inspection report: schema objects, row counts, current state, history, and useful queries.
- `audit_runtime_sqlite.py` validates required schema versions, text/key quality, restore coverage, raw-to-canonical row counts, baseline state, and event classes.
- Both scripts decode UTF-8, CP1250, and Latin-1. Use them instead of assuming raw SQLite GUI rendering proves an encoding or data bug.
- Schema 21 expects no persisted `v_*` views in `sqlite_master`. Reporting must query physical `runtime_*`, `mmo_*_current`, and baseline tables directly.
- Schema 22 expects `mmo_save_slots`, `mmo_save_slot_snapshots`, and the full `mmo_save_slot_*` component table set. Use these tables to verify save/load parity across game restarts.

After a gameplay test, run the audit first. Treat an invariant error as a persistence defect; treat an informational missing event as a test-coverage gap.
