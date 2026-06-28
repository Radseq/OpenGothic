# 12 MySQL Dispatch Dev Fixture

Purpose: support local Step 35 resolved-dispatch testing when the OpenGothic client state and MySQL projection are already out of sync.

Observed case:
- OpenGothic emitted valid live `pickup_world_item`, `equip_character_item`, `unequip_character_item` envelopes.
- Receiver accepted and enqueued them.
- Resolved worker refused pickup because `world_entity_state.lifecycle_state='removed'` and `item_instances.lifecycle_state='archived'` for the same engine world item key.

Interpretation:
- This is not a hook/UDP/outbox failure.
- This proves the server is validating against current MySQL projection.
- It also proves the dev client was not started from the same authoritative state as MySQL.

Production rule:
- Do not solve this in production by blindly reactivating rows.
- A production server must either start clients from server state or reject stale client actions.
- Full readiness still requires real parity artifacts and replay from `content baseline + world_event_journal`.

Dev-only fixture:
- `tools/prepare_mmo_dispatch_dev_fixture.py` can explicitly reactivate only the loose world item rows referenced by one receiver session key.
- It updates `world_entity_state` back to `active` and moves the matching `item_instances` row back to `owner_type='world_entity'` for local dispatch testing.
- It deletes any character inventory/equipment rows for that item instance to prevent duplicate ownership.
- It resets matching failed/claimed outbox rows to `pending`.
- It writes `dev_fixture_restore_*` markers into JSON payloads.

This is intentionally not a production repair path. Use it only to prove:

```text
receiver outbox -> resolver -> mmo_pickup_world_item -> mmo_equip_character_item -> mmo_unequip_character_item
```

Recommended flow:

```bash
python3 tools/prepare_mmo_dispatch_dev_fixture.py   --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo"   --session-key local-dev-PC_HERO_STEP35V2
```

If the dry run shows exactly the intended rows:

```bash
python3 tools/prepare_mmo_dispatch_dev_fixture.py   --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo"   --session-key local-dev-PC_HERO_STEP35V2   --apply
```

Then run the resolved worker with the same session prefix:

```bash
python3 tools/run_mmo_resolved_action_worker.py   --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo"   --worker-id dev-resolved-worker   --session-key local-dev-PC_HERO_STEP35V2   --max-actions 10
```


## Step 35 v2.3 bag-index collision fix

Observed failure:

```text
ERROR 1062 (23000): Duplicate entry ... for key 'character_inventory.character_inventory_bag_uk'
```

Interpretation:
- The receiver/outbox/fixture path was already past world-item active validation.
- The dev worker defaulted pickup target `bag_index` to `0`.
- `character_inventory` enforces a unique bag index per character, so inserting a picked-up item into an occupied bag slot correctly failed.

Fix:
- `tools/run_mmo_resolved_action_worker.py` now allocates the first free `character_inventory.bag_index` from the server projection immediately before calling `mmo_pickup_world_item(...)`.
- Client `bag_index` is not trusted as authoritative. Only explicit `server_bag_index` in an outbox payload overrides this for controlled tests.
- `tools/inspect_mmo_action_resolution.py` prints occupied bag slots and `next_free_bag_index` for pickup actions.

Production note:
This is still a dev worker fix. A production MMO server should allocate inventory positions under server authority, ideally inside the server/domain transaction or in the stored procedure contract, not from a client-supplied slot.
