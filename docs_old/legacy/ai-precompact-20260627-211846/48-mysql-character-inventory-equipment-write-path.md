# MySQL Character Inventory/Equipment Write Path

Goal: add the first server-owned character inventory and equipment mutation path on top of the MySQL production schema.

This step assumes migrations `001..005` are already applied and the database has been bootstrapped from `runtime/g2notr.sqlite`.

## Files

- `db/migrations/mysql/production/006_character_inventory_equipment_write_path.sql`
- `tools/check_mysql_character_inventory_equipment_write_path.py`

## New table

`character_inventory_audit` records accepted inventory/equipment mutations after the event has been appended and the projection has been updated.

It records:

- active session;
- source and optional target character;
- world instance;
- event id and idempotency key;
- item instance id/key;
- optional equipment slot;
- source/target bag indexes;
- owner before/after;
- server tick and raw JSON payload.

The current-state projections remain:

```text
item_instances
character_inventory
character_equipment
```

The durable ordered mutation source remains:

```text
world_event_journal
```

## New procedures

`mmo_transfer_character_item(...)` implements full item-instance transfer from the active session character to another active character in the same realm. It removes any source equipment row for the item, moves `item_instances.owner_id`, deletes the source `character_inventory` row and inserts the target `character_inventory` row.

`mmo_equip_character_item(...)` equips a character-owned inventory item into a named equipment slot. For this first slice, an occupied slot is rejected; swap/replace rules should be added as a later explicit mechanic.

`mmo_unequip_character_item(...)` removes the item from the named equipment slot while keeping the item in `character_inventory`.

## Idempotency rule

A repeated call with the same `world_instance_id + idempotency_key` returns the original event and does not apply the projection mutation again.

Reusing the same idempotency key for a different inventory/equipment operation fails.

## Deliberate limitations

This migration supports full item-instance transfer only. Partial stack split/merge is deliberately excluded because it needs a separate deterministic item-instance split contract.

Equipment validation is structural only. The procedure does not yet prove that a template is really a weapon, armor, ring, rune or torch. That requires stronger item classification from Daedalus flags and should be added before real gameplay exposure.

## Smoke test

```bash
python3 tools/check_mysql_character_inventory_equipment_write_path.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --account-name local-import \
  --character-key PC_HERO \
  --run-smoke
```

The smoke test does:

```text
login
seed synthetic character-owned item
equip torch slot
idempotent equip retry
unequip torch slot
idempotent unequip retry
seed synthetic target character
transfer item to target character
idempotent transfer retry
logout
```

The smoke test leaves synthetic audit/events and a synthetic item on the target character. This is intentional for event-sourced diagnostics.

## Next step

After this works, the next mechanic should be container inventory and interactive state:

```text
active session -> interactive/container key -> transfer item to/from container -> world/character projections -> semantic event
```

That step should use `Interactive::inventory()` and committed container/door/lock state boundaries later in the C++ layer, not periodic full-world diff inference.
