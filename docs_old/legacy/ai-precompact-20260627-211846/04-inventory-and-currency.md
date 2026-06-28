# Inventory And Currency

`Inventory::iterator(T_Inventory)` exposes normal items, including gold. `T_Trade` intentionally hides gold.

- Preserve `amount`, `iterator_count`, `equipped`, `equip_count`, and `slot`; they have different Gothic semantics.
- `runtime_character_inventory` keeps source-faithful item rows.
- Gold is also persisted as the explicit character wallet: `runtime_character_wallet` with key `g2notr:gold`.
- Compare both the item row and wallet during migration validation; do not double-spend by treating them as independent gameplay balances.

`GameScript::goldId()` and `GameScript::currencyName()` define the active currency item and localized name.

