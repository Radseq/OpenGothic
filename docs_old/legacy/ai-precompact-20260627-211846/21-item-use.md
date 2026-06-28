# Item Use And Equipment

Files: `game/game/inventory.cpp`, `game/world/objects/npc.cpp`.

- `Npc::useItem` delegates to `Inventory::use`.
- `Inventory::use` routes weapons, shields, armor, belts, amulets, rings, runes, torches, and consumables by item flags.
- Consumables invoke their Daedalus `on_state` function; torches may consume one item after a successful toggle.
- `Inventory::transfer` is the shared primitive for inventory/container transfers.
- `equip` and `unequip` mutate equipment and may affect derived NPC stats.

Persist only after the operation succeeds. Emit distinct item-use, consume, equip, unequip, transfer, buy, and sell events; do not infer all of them from a later inventory diff.

