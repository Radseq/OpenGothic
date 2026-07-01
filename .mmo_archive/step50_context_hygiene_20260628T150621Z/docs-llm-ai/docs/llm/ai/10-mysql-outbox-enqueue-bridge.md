# 10 MySQL Outbox Enqueue Bridge

Purpose: move from JSONL-only server-boundary smoke to a DB-visible server handoff while keeping production ownership correct.

Implemented:
- `tools/run_mmo_action_receiver.py` supports `--enqueue-outbox` with `--mysql-url`.
- It receives UDP semantic envelopes from OpenGothic, validates shape, de-duplicates idempotency keys, writes accepted server JSONL and enqueues into `mmo_server_action_outbox`.
- It uses `mmo_login_character(...)` for a dev active session and `mmo_enqueue_server_action(...)` for the handoff.
- `tools/check_mmo_action_receiver_outbox.py` reports action/status counts, latest rows, dispatch contract gaps and required-kind coverage.
- `tools/replay_mmo_actions_to_receiver.py` replays local JSONL into the receiver to test duplicates/idempotency without relaunching the game.

Architecture boundary:
```text
OpenGothic client process
  -> bounded semantic action queue
  -> async UDP dev transport
  -> receiver/server process
  -> mmo_server_action_outbox
```

Still forbidden:
```text
OpenGothic gameplay thread -> MySQL
```

Current limitation:
- OpenGothic envelopes identify engine objects with world/item stable keys and template symbols.
- MySQL write procedures often require DB UUIDs such as `item_instance_id`.
- Therefore receiver outbox payloads may be `dispatch_ready=false`. This is a useful diagnostic, not a failure.

Next server work:
1. Add a resolver that maps engine keys from the envelope to MySQL projection rows for the active world/session.
2. Fill exact procedure payload fields such as `item_instance_id`, bag index and target owner.
3. Dispatch one slice, probably `pickup_world_item`, through existing `mmo_*` procedures.
4. Verify idempotent retry returns the same event/action without duplicate item movement.
5. Only then mark a parity scenario as real evidence.

Operational notes:
- Start the receiver before the game and use receiver `--truncate` to clear server JSONL.
- Do not remove the receiver JSONL file after the receiver starts.
- Use local client JSONL and server JSONL together to compare client emission vs server receipt.
- Keep UDP dev-only. Reliable transport/acks/retry belong to the later production networking layer.

## v2 resolved worker extension

Outbox enqueue is now followed by an optional resolved dispatch worker:

```text
mmo_server_action_outbox pending
  -> run_mmo_resolved_action_worker.py
  -> resolver(engine key -> DB projection row)
  -> mmo_pickup_world_item / mmo_equip_character_item / mmo_unequip_character_item
  -> mmo_mark_server_action_applied / failed
```

The resolver is intentionally strict:
- world item pickup resolves `world-item:<world>:pid:<pid>:sym:<sym>` to the imported MySQL `world_entity_state.entity_key`, including older `world_item:<world>:<pid>:<sym>:<slot>` shapes;
- character equip resolves a character-owned `item_instance` by template symbol and source persistent id;
- unequip resolves by semantic equipment slot only;
- ambiguous matches fail. Do not fake an item UUID to make the smoke green.
