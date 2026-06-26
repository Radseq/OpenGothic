# Item Identity And State

Files: `game/world/objects/item.h`, `game/world/objects/item.cpp`.

- `clsId()` is the Daedalus item template symbol, not an item instance ID.
- `persistentId()` is the durable source ID for world-item identity.
- Inventory rows need template symbol plus slot/equipment context; world item rows need persistent ID plus template/script data.
- `amount`, `equipCount`, and `slot` are independent fields.
- `isGold()` compares the item template with `GameScript::goldId()`.
- `Item::save` is native save compatibility; the database should retain structured fields rather than a blob.

Never use array index or item display name as a durable key.

