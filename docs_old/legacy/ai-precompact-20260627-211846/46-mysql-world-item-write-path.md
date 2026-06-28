# MySQL World Item Write Path

Goal: add the first server-owned loose world item mutation path on top of the MySQL production schema.

This step assumes migrations `001..004` are already applied and the database has been bootstrapped from
`runtime/g2notr.sqlite`.

## Files

- `db/migrations/mysql/production/005_world_item_write_path.sql`
- `tools/check_mysql_world_item_write_path.py`

## New table

`world_item_audit` records accepted world-item mutations after the event has been appended and the
projection has been updated. It is audit metadata, not the source of truth. The durable source remains:

```text
world_event_journal + deterministic projection rules
```

## New procedures

`mmo_pickup_world_item(...)` implements the first loose item pickup slice:

```text
active session
-> validate active world item entity
-> validate active item instance owned by world_entity
-> append world_item_picked_up
-> move item_instances owner to character
-> insert character_inventory row
-> mark world_entity_state as removed
-> write world_item_audit
```

`mmo_remove_world_item(...)` implements server-owned world item removal/despawn:

```text
active session
-> validate active world item entity
-> validate active item instance owned by world_entity
-> append world_item_removed
-> archive item_instances row under system ownership
-> mark world_entity_state as removed
-> write world_item_audit
```

## Idempotency rule

A repeated call with the same `world_instance_id + idempotency_key` returns the original event and item
instance. It does not move the item, insert inventory, or write audit a second time.

## Deliberate limitation

Migration 005 supports full-stack pickup only. Partial stack split is deliberately blocked with a clear
SQL error because it needs an additional item-instance split rule and replay contract.

## Smoke test

```bash
python3 tools/check_mysql_world_item_write_path.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --account-name local-import \
  --character-key PC_HERO \
  --run-smoke
```

The smoke test creates two synthetic `smoke:*` loose world items in the current character world:

1. one item is picked up into `character_inventory`;
2. one item is removed/archived;
3. both operations are retried with the same idempotency key;
4. event/audit counts must remain one per operation.

The synthetic event/audit rows are intentionally left in the database. They are test provenance.

## Next step

After this works, the next mechanic should be character inventory/equipment operations:

```text
character item transfer/equip/unequip
```

That should cover inventory-to-inventory transitions and equipment-slot uniqueness. Container inventory
should come after that, because containers reuse the same transfer semantics plus interactive ownership.
