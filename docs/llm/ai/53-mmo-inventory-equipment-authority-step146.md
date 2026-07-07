# Step146 - MMO inventory equipment authority

Goal: move a safe slice of Gothic inventory semantics to the C++ server without
copying single-player runtime internals 1:1.

Client semantics checked first:
- `Inventory::use` decides equipment slot from Gothic item flags:
  shield, melee weapon, ranged weapon, rune/numslot, armor, belt, amulet, ring
  and torch.
- `Inventory::slotId` reports only melee as `1`, ranged as `2` and numslots as
  `3..10`. Armor, belt, amulet, rings and shield can be reported as `255`.
- `Inventory::setSlot` fires `on_equip`/`on_unequip`, changes visuals/stats and
  then emits MMO hooks. Visuals/animations remain client runtime state.

Server changes:
- `mmo_server_inventory_authority.h` now contains Gothic item flag constants,
  known equipment slot names and constexpr slot validation.
- Numeric slots `3..10` are normalized to the server `rune` slot instead of
  `unknown`.
- `equip_character_item` now resolves the character item first, reads
  `content_item_templates.raw_payload.main_flag` and `.flags`, then validates
  that the requested or inferred equipment slot matches Gothic semantics.
- If the client sends `255`/`unknown`, the server infers armor/belt/amulet/ring
  slots from item flags. Rings follow Gothic's first-free behavior by choosing
  `ring_left` before `ring_right`.
- `unequip_character_item` resolves `unknown` by item id against current
  `character_equipment`, so armor/ring/belt unequip no longer targets the
  synthetic `unknown` slot.
- DB payload mapping now writes normalized equipment slots for string and numeric
  payload forms.
- `consume_item` now has a direct C++/DB path through
  `mmo_consume_character_item`. It decrements character inventory stacks,
  removes equipment when the last item is consumed, marks the item instance as
  consumed and journals `character_item_consumed`.

Still intentionally not server-owned in this step:
- `checkCondUse` and `checkCondRune` stat/script requirements.
- `on_state` consumable effects, food/potion logic and torch animation state.
- Trade price/merchant inventory policy.
- Active weapon state and visual changes; those remain separate combat/replication
  domains.

This is a narrow server-authoritative layer: item ownership still uses existing
DB procedures, but equipment slot correctness is now enforced before mutation.
