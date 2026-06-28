# MySQL Item Stack Write Path

Migration: `db/migrations/mysql/production/013_item_stack_write_path.sql`.

Adds explicit partial-stack semantics:

```text
mmo_split_character_item_stack(...)
mmo_merge_character_item_stack(...)
```

This closes the deliberate limitation from world item, inventory/equipment, container and trade slices.
A partial pickup/buy/sell/consume operation should first split a stack into its own `item_instance`, then
apply the target mutation to that instance.

The invariant after split/merge is:

```text
character_inventory.amount == item_instances.quantity
item_instances.owner_type == 'character'
item_instances.owner_id == character_id
```
