# 14 Step 36 Vertical Slice Evidence

Purpose: convert the successful Step 35 resolved dispatch into a repeatable read-only evidence artifact.

Current proven runtime path:

```text
OpenGothic hook
  -> async UDP dev transport
  -> receiver/server boundary
  -> mmo_server_action_outbox
  -> run_mmo_resolved_action_worker.py
  -> mmo_* stored procedures
  -> world_event_journal
  -> MySQL current-state projections
```

`tools/check_mmo_step36_vertical_slice.py` checks that this path produced consistent durable state for the first inventory/equipment slice:

- `pickup_world_item` rows in outbox are `applied`;
- `equip_character_item` and `unequip_character_item` rows in outbox are `applied`;
- matching `world_event_journal` events exist and are server-sourced;
- picked item instances are character-owned and active;
- picked world item entities are not still active in the world projection;
- after unequip, the item remains character-owned/in inventory and is not active in `character_equipment`.

Optional evidence inputs:

- client JSONL: verifies local hook emission and idempotency-key uniqueness;
- server JSONL: verifies receiver evidence;
- runtime SQLite file: records table counts and sample hashes for relevant inventory/equipment/world tables;
- native save file/directory: records file hashes only.

Important limitation:

```text
Step36 v1 OK != full restore parity passed
```

This checker does not semantically replay a native `.sav`, nor does it prove DB-only load parity. It is a vertical-slice evidence artifact proving that the already-dispatched server events and projections are internally consistent.

Do not update the global parity registry to passed from this alone, especially when a dev fixture was used to realign world items before dispatch.

Recommended command after the successful Step35V2 run:

```bash
python3 tools/check_mmo_step36_vertical_slice.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP35V2 \
  --client-jsonl runtime/mmo_actions.jsonl \
  --server-jsonl runtime/mmo_server_actions_step35v2.jsonl \
  --sqlite runtime/g2notr.sqlite \
  --require-two-pickups \
  --output runtime/mmo_step36_vertical_slice_STEP35V2.json
```

Expected status for the current successful run:

```text
outbox pickup_world_item/applied=2
outbox equip_character_item/applied=1
outbox unequip_character_item/applied=1
journal world_item_picked_up/inventory/server=2
journal character_item_equipped/equipment/server=1
journal character_item_unequipped/equipment/server=1
projection checks passed
```

Next real Step 36 work:

1. Start the client from a DB projection that is not dev-fixture-repaired.
2. Capture native `.sav`, runtime SQLite save-slot snapshot, server JSONL and MySQL projection from the same scenario.
3. Implement a semantic comparator for the scenario rather than only hashing files.
4. Only then mark `world_item_pickup` and `equip_unequip` parity rows as real passed evidence.

## v1.2 server JSONL recovery from outbox

If receiver JSONL was lost, empty or truncated, strict Step36 correlation can recover server-side acceptance evidence from `mmo_server_action_outbox.request_payload` using `--recover-server-jsonl-from-outbox`. This is valid only as Step36 evidence because the outbox row was created by the receiver after validation/enqueue. It is not a replacement for production transport logs or replay input.
