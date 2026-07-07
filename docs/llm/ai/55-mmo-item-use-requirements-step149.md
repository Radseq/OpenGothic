# Step149 - MMO item use requirements without new SQL surfaces

Goal: move another low-risk slice of Gothic client inventory semantics to C++
server modules while avoiding new views/procedures.

Client semantics checked first:
- `Inventory::setSlot` calls `Item::checkCondUse` before equipping.
- `checkCondUse` iterates `cond_atr/cond_value` and rejects the item when the
  actor attribute is below a non-zero required value.
- `checkCondRune` compares `Npc::mageCycle()` with `item.mag_circle`.
- `ConsumeMana` packets are emitted from player mana attribute decreases.

Server changes:
- `mmo_server_inventory_authority.h` now has constexpr Gothic attribute ids,
  `CharacterUseStats`, `ItemUseRequirements` and
  `validateItemUseRequirements`.
- `mmo_udp_server_inventory_resolution.inl` reads character stats and item use
  requirements from existing tables and validates them before
  `equip_character_item`.
- No new SQL procedures or views were added for item conditions.
- `mmo_server_combat_authority.h` now has `resolveResourceSpend`, a pure helper
  for server-side resource consumption.
- `mmo_udp_server_direct_combat_apply.inl` resolves current mana from
  `character_stats` for `consume_mana` and computes `before/after/delta` on the
  server instead of trusting client values.

Data expectations:
- Item requirements are read from `content_item_templates.raw_payload` keys such
  as `cond_atr`, `cond_value` and `mag_circle`.
- Mage circle is read from `character_stats.raw_stats` keys such as
  `talent_skill[7]`, `talentSkills[7]`, `talents.mage.skill` or `mage_skill`.
- Missing requirement data defaults to zero, matching ordinary items with no
  condition. Missing mage-circle data means the character has circle `0`.

Still separate:
- Importing richer item template data from scripts into MySQL.
- Scripted `on_state` effects for consumables/potions/food.
- Admin/script force-equip capability. Player payloads cannot bypass these
  requirements.
