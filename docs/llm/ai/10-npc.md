# NPC Persistence Surface

Files: `game/world/objects/npc.h`, `game/world/objects/npc.cpp`.

- `persistentId()` is the stable NPC source identity component.
- `PersistentState` contains attributes, protections, talents, missions, AI variables, guilds, progression, attitudes, and death.
- `restorePersistentState` is the approved DB restore path for that component.
- `PersistentInventoryItem` plus `restorePersistentInventory` restore NPC/HERO inventory and equipment through `Inventory` APIs.
- `currentAiStateFunction`, `currentAiStateName`, `target`, `stateOther`, and `stateVictim` provide AI relation capture.
- Full active AI/path queues are transient; restore only validated follow/escort checkpoints.

Do not restore by writing `hnpc` fields from SQLite outside the explicit persistence methods.

