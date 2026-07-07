# Step148 - MMO NPC trade inventory authority

Goal: move the low-risk Gothic NPC trade semantics to the server as a separate
trade/inventory bridge.

Client semantics checked first:
- `Npc::sellItem` ignores gold, transfers the item stack from hero inventory to
  NPC inventory and then adds `sellPrice * count` gold to the seller.
- `Npc::buyItem` ignores gold, clamps count by available gold when price is
  positive, transfers the item stack from NPC inventory to hero inventory and
  then subtracts or adds gold depending on the sign of the unit price.
- MMO hooks emit `trade_buy_from_npc` and `trade_sell_to_npc` with NPC identity,
  item symbol/persistent id, amount, unit price, price total and gold wallet
  before/after observations.

Server changes:
- `InventoryAuthority` now has a small constexpr trade validation contract:
  NPC key, currency key, stack amount, bag index and bounded signed trade price.
- `mmo_udp_server` direct inventory apply handles `TradeSellToNpc` and
  `TradeBuyFromNpc` without using the legacy generic transfer path.
- Trade NPC identity is resolved through a dedicated `resolveTradeNpcEntityKey`
  helper, so trade logic does not know individual client key formats.
- Selling resolves the character item and calls `mmo_trade_sell_to_npc`.
- Buying resolves the NPC-owned `world_inventory` item and calls
  `mmo_trade_buy_from_npc`.
- `server/sql/step148_trade_inventory_bridge.sql` adds atomic trade procedures:
  stack split/whole-stack ownership transfer, wallet update, world event and
  audit rows.

Still intentionally separate:
- Merchant price policy remains client-observed/server-validated, not fully
  recalculated from scripts yet.
- Gold is represented through the existing `character_wallets` bridge. A later
  pass can reconcile Gothic gold item stacks with wallet state.
- Shop inventory refresh/restock rules are not part of this step.
