# Runtime DB Save Slots

Goal: keep legacy save/load working while building a DB state model that can replace file saves later.

- Entry points: `MainWindow::loadGame()` passes the legacy slot path into `GameSession(Serialize&, sourceSlot)`. `MainWindow::saveGame()` still writes the native `.sav`, then calls `GameSession::recordMmoSaveSlot()`.
- Runtime owner: `GameSession` creates `MmoRuntimeSqlite(path, intervalMs, restoreState, captureBaseline, saveSlotPath)`.
- Schema: version 22 adds `mmo_save_slots`, `mmo_save_slot_snapshots`, and component tables named `mmo_save_slot_*`.
- Save path: `MmoRuntimeSqlite::recordSaveSlot()` flushes/materializes `mmo_*_current`, inserts a new snapshot row, copies every current component table into the matching `mmo_save_slot_*` table with `snapshot_id`, and updates `mmo_save_slots.current_snapshot_id`.
- Load path: `MmoRuntimeSqlite::open()` first tries `restoreSaveSlotSnapshot()` when a legacy slot path is known. A missing DB snapshot disables DB restore for that load so old `.sav` behavior stays authoritative.
- Key format: `legacy-save-slot:<source_slot_path>`. This is a bridge key, not the final MMO GUID/account identity model.

Invariant for future work: never restore one global `mmo_*_current` state over an unrelated legacy save slot. A DB restore must be tied to a slot/account/realm/character snapshot identity.
