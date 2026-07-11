# Context Refresh Procedure

1. Read source, tests and CMake for the changed subsystem.
2. Update only durable facts in `current-state.md`.
3. Update the smallest actionable edge in `next-work.md`.
4. Update contracts/architecture only when a stable boundary changed.
5. Keep historical phase notes compact and explicitly non-canonical.
6. Run `python3 tools/check_llm_context.py --strict`.
7. Rebuild and verify the optional SQLite index if it is used.

Exclude terminal logs, credentials, private assets, generated snapshots, runtime
DB data and speculative performance claims.
