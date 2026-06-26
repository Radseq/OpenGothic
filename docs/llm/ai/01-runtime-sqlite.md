# Runtime SQLite

`game/game/mmoruntimesqlite.cpp` is the local persistence bridge, not the final MMO server.

- `runtime_*`: engine capture and diagnostics.
- `mmo_*_current`: physical restore projection.
- `mmo_world_baseline_*`: immutable New Game content baseline.
- `mmo_save_slots` and `mmo_save_slot_*`: durable per-legacy-slot DB snapshots.
- `runtime_events`: append-only observed event journal.
- Schema 20 separates currency into `runtime_character_wallet` and `mmo_character_wallet_current`.
- Schema 21 removes persisted SQL views from the database file. Runtime SQL may create `TEMP VIEW` helpers inside one SQLite connection, but the durable contract is tables only.
- Schema 22 adds per-save-slot snapshots. Native `.sav` remains the legacy carrier, but after a successful save the runtime DB records a complete snapshot of `mmo_*_current` for that slot. Loading a `.sav` restores DB state only from the matching slot snapshot; if it is missing, legacy save state is kept.

Normal ticks must write deltas only. Full projection/materialization is allowed at controlled startup or shutdown, never on the gameplay cadence.
