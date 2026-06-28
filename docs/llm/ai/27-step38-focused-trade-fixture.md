# 27 Step38 Focused Trade Fixture

Context:
- Step38 combat/resource E2E is green from a real captured JSONL stream.
- The remaining Step38 acceptance surface is trade: `Npc::buyItem` and
  `Npc::sellItem` through receiver -> outbox -> resolver -> MySQL procedures.
- The existing worker buy resolver looked at generic `world_inventory`, but the
  current MySQL schema has dedicated `npc_trade_inventory` for vendor stock.

Patch:
- `run_mmo_resolved_action_worker.py` now resolves `trade_buy_from_npc` through
  `npc_trade_inventory` first. It still keeps a legacy `world_inventory` fallback
  for old local bridge experiments.
- The resolver matches vendor stock by `npc_entity_key`, `item_symbol`,
  optional `vendor_item_persistent_id`, `currency_key`, active item lifecycle,
  available stock state and sufficient amount.
- `prepare_mmo_step38_dev_fixture.py` now handles trade rows in addition to
  combat/resource rows:
  - seeds/reactivates vendor NPC rows;
  - seeds `npc_trade_inventory` stock for buy actions;
  - seeds character inventory rows for sell actions;
  - raises the character wallet floor for buy actions.
- `check_mmo_step38_trade_combat_mysql.py` now verifies the expected journal
  event for each required Step38 kind, not just the applied outbox row.

Meaning:
- This gives a focused local E2E path for real in-game buy/sell captures.
- It is not final MMO authority and not `.sav + SQLite + MySQL` parity.
- Production still needs server-side trade intent validation, price/stock
  authority and anti-duplication before accepting client trade requests.
