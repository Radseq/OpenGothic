# Semantic Event Hooks

Current periodic capture is a safety net. The MMO server needs explicit events at mutation boundaries.

- World item pickup/removal: `World::takeItem`, `World::removeItem`.
- NPC lifecycle: `World::removeNpc` and NPC death handling.
- Inventory transfer/equipment: `Inventory::transfer`, `Inventory::equip`, `Inventory::unequip`.
- Trade: `Npc::sellItem`, `Npc::buyItem`.
- Spells: `Npc::commitSpell` and projectile/spell world paths.
- Interactives: committed container, door, lock, and state transitions in `Interactive`.
- Quest/dialog/script changes: `GameScript` and `GameSession` dialog hooks.

Emit one transactional gameplay event after a mutation succeeds. Snapshot diffs remain validation, not the authoritative event source.

