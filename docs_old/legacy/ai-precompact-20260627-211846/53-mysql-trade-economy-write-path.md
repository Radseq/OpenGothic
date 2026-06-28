# MySQL Trade/Economy Write Path

Migration: `db/migrations/mysql/production/011_trade_economy_write_path.sql`.

This adds the first server-owned NPC trade slice:

```text
active session
-> npc_trade_inventory
-> trade_buy_from_npc / trade_sell_to_npc event
-> character_wallets
-> item_instances
-> character_inventory
-> trade_economy_audit
```

The NPC inventory is represented by `npc_trade_inventory` instead of extending `item_instances.owner_type`
with a new enum value. Existing production schema restricts `owner_type`, so trade stock is held under
`item_instances.owner_type='system'` and `npc_trade_inventory` becomes the authoritative stock projection
for vendor ownership.

Only full `item_instance` buy/sell is supported here. Partial stack buy/sell must go through the explicit
stack split/merge contract from migration 013 before trade is applied.
