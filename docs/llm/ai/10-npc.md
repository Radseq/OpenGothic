# NPC Persistence Surface

Files: `game/world/objects/npc.h`, `game/world/objects/npc.cpp`.

- `persistentId()` is the stable NPC source identity component.
- `PersistentState` contains attributes, protections, talents, missions, AI variables, guilds, progression, attitudes, and death.
- `restorePersistentState` is the approved DB restore path for that component.
- `PersistentInventoryItem` plus `restorePersistentInventory` restore NPC/HERO inventory and equipment through `Inventory` APIs.
- `currentAiStateFunction`, `currentAiStateName`, `target`, `stateOther`, and `stateVictim` provide AI relation capture.
- Runtime SQL schema 23 stores `target_key`, `state_other_key`, and `state_victim_key` in AI state/history. Use these for diagnostics; restore only checkpointed follow/escort relations.
- Production stat reads should use `mmo_unit_stat_current` for normalized rows and `mmo_unit_stat_sheet_current` for the wide character/NPC/mob sheet. Schema 25 sheet columns include progression (`experience_next`, `learning_points`) and attitude (`permanent_attitude`, `temporary_attitude`). `runtime_npc_stats` is the raw engine EAV capture.
- Full active AI/path queues are transient; restore only validated follow/escort checkpoints.

Do not restore by writing `hnpc` fields from SQLite outside the explicit persistence methods.
