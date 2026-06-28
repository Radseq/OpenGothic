# MySQL Container/Interactive Write Path

Goal: add the first server-owned container inventory and interactive state mutation path on top of the MySQL production schema.

This step assumes migrations `001..006` are already applied and the database has been bootstrapped from `runtime/g2notr.sqlite`.

## Files

- `db/migrations/mysql/production/007_container_interactive_write_path.sql`
- `tools/check_mysql_container_interactive_write_path.py`

## New table

`world_interactive_audit` records accepted container and interactive mutations after the event has been appended and the projection has been updated.

It records:

- active session;
- character;
- world instance;
- event id and idempotency key;
- interactive/container entity key;
- optional item instance id/key;
- owner before/after for container transfers;
- row version before/after for interactive state;
- state JSON before/after;
- server tick and raw JSON payload.

The current-state projections remain:

```text
world_entity_state
world_inventory
item_instances
character_inventory
character_equipment
```

The durable ordered mutation source remains:

```text
world_event_journal
```

## New procedures

`mmo_take_container_item(...)` implements full item-instance movement from an active interactive/container into the active session character inventory:

```text
active session
-> active world_entity_state interactive
-> world_inventory row
-> item_instances.owner_type = container
-> append container_item_taken
-> delete world_inventory row
-> item_instances.owner_type = character
-> insert character_inventory row
-> world_interactive_audit
```

`mmo_put_container_item(...)` implements full item-instance movement from the active session character inventory back into an active interactive/container:

```text
active session
-> active world_entity_state interactive
-> character_inventory row
-> item_instances.owner_type = character
-> append container_item_put
-> remove optional equipment row
-> delete character_inventory row
-> item_instances.owner_type = container
-> insert world_inventory row
-> world_interactive_audit
```

`mmo_update_interactive_state(...)` implements durable interactive state changes:

```text
active session
-> world_entity_state interactive
-> append interactive_state_changed
-> update lifecycle_state/state_json/row_version
-> world_interactive_audit
```

The first slice persists `state_id`, `state_count`, `state_mask`, `locked`, and `cracked` in `world_entity_state.state_json`. Later C++ integration must still apply this through `Interactive::restorePersistentState`/ownership API rather than writing engine fields directly.

## Idempotency rule

A repeated call with the same `world_instance_id + idempotency_key` returns the original event and does not apply the projection mutation again.

Reusing the same idempotency key for another container/interactive operation fails.

## Deliberate limitations

This migration supports full item-instance take/put only. Partial stack split/merge is deliberately excluded because it needs a separate deterministic item-instance split contract.

Lockpicking rules are not implemented here. `needToLockpick` depends on keys, lock code and cracked state, so this procedure records the resulting committed state only. Gameplay validation should later live in the server action layer.

## Smoke test

```bash
python3 tools/check_mysql_container_interactive_write_path.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --account-name local-import \
  --character-key PC_HERO \
  --run-smoke
```

The smoke test does:

```text
login
seed synthetic interactive/container + contained item
take container item
idempotent take retry
interactive state change
idempotent state retry
put item back into container
idempotent put retry
logout
```

The smoke test leaves a synthetic interactive/container, one contained item, and event/audit rows. That is intentional for event-store traceability.

## Next step

After this works, the next mechanic should be quest/dialog/script progress:

```text
active session -> quest/dialog/script committed change -> semantic event -> character/world script projection
```

This is especially important for one-shot interactions such as bookstands, read flags, learned dialog, quest log entries, and XP/script rewards.
