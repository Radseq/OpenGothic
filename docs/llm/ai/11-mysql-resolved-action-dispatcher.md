# 11 MySQL Resolved Action Dispatcher

Purpose: execute receiver-enqueued semantic actions only after a server-side resolver maps OpenGothic engine keys to MySQL projection rows.

Current flow:

```text
OpenGothic hook
  -> async UDP dev transport
  -> receiver/server boundary
  -> mmo_server_action_outbox
  -> run_mmo_resolved_action_worker.py
  -> mmo_* stored procedure
  -> world_event_journal + current projection
```

Implemented worker scope:
- `pickup_world_item` / `remove_world_item`: resolve `world-item:<world>:pid:<persistent_id>:sym:<symbol>` to `world_entity_state.entity_key`, require exactly one active world item, then call migration 005 procedures.
- `equip_character_item`: resolve active character-owned `item_instance_id` from item symbol and persistent-id hints, then call migration 006 equip procedure.
- `unequip_character_item`: resolve by equipment slot and call migration 006 unequip procedure.

Step 35 v2.1 hardening:
- Worker should be run with `--session-key <prefix>` during dev replay. Without it, the stock DB claim procedure can pick old pending rows from previous smoke sessions.
- Default behavior stops after the first failure. This prevents dependent actions such as equip/unequip from being applied after an earlier pickup failed.
- `--continue-on-error` is available only for broad diagnostics.
- `--reset-matching-failed` may reset failed/dead-letter/claimed rows for the selected `--session-key`; it never resets already applied rows.
- Resolver now fails before calling `mmo_pickup_world_item` when a matched world item row is not active. This turns `world item entity is not active` into an explicit resolver/projection mismatch.

Important interpretation:
- `world item resolved but is not active` usually means the MySQL projection is not the same world state as the client that produced the JSONL, or that the same item was already consumed in an earlier DB test.
- Do not force-dispatch against inactive items. Either replay against a fresh matching DB projection, use a new controlled test item, or rebuild/import the DB state from a save/runtime snapshot that matches the client test.
- A failed pickup makes later equip of the picked item expected to fail; do not treat the later failure as an independent equipment bug until pickup succeeds.

Diagnostics:
- `tools/inspect_mmo_action_resolution.py` is read-only and prints outbox rows, world item candidates, item instance candidates and equipment slots for one `--session-key`.
- `tools/check_mmo_action_dispatch_results.py` is a status checker; it now uses `event_uuid` from `v_server_action_worker_latest_results` and should not crash on the worker result view.

This is still a development bridge. Production networking, authoritative validation and replay/parity proof are later gates.


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
