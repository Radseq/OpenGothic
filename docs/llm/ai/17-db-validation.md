# Runtime DB Validation

Files: `tools/check_runtime_sqlite.py`, `tools/audit_runtime_sqlite.py`.

- `check_runtime_sqlite.py` is an inspection report: schema objects, row counts, current state, history, and useful queries.
- `audit_runtime_sqlite.py` validates required schema versions, text/key quality, restore coverage, raw-to-canonical row counts, baseline state, and event classes.
- Both scripts decode UTF-8, CP1250, and Latin-1. Use them instead of assuming raw SQLite GUI rendering proves an encoding or data bug.
- Schema 21 expects no persisted `v_*` views in `sqlite_master`. Reporting must query physical `runtime_*`, `mmo_*_current`, and baseline tables directly.
- Schema 22 expects `mmo_save_slots`, `mmo_save_slot_snapshots`, and the full `mmo_save_slot_*` component table set. Use these tables to verify save/load parity across game restarts.
- Schema 23 expects AI relation context columns on `runtime_npc_ai_state/history`: `state_other_key` and `state_victim_key`.
- Schema 24 expects story/chapter tables: `runtime_story_progress_current`, `runtime_story_progress_history`, `runtime_chapter_intro_events`, `mmo_character_story_progress_current`, and `mmo_save_slot_character_story_progress`.
- Schema 25 expects `experience_next`, `learning_points`, `permanent_attitude`, and `temporary_attitude` on both `mmo_unit_stat_sheet_current` and `mmo_save_slot_unit_stat_sheet`.
- `audit_runtime_sqlite.py` checks raw SQLite TEXT payloads with `CAST(column AS BLOB)` and treats invalid UTF-8 as an error. CP1250 fallback is only for diagnostics, not for durable storage.
- `item_quantity_changed` is required only when inventory history shows a same-template stack quantity change; otherwise the audit reports the missing scenario as test coverage.

After a gameplay test, run the audit first. Treat an invariant error as a persistence defect; treat an informational missing event as a test-coverage gap.
