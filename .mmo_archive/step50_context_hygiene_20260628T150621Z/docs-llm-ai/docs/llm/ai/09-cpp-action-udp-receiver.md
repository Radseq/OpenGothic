# 09 C++ Action UDP Receiver

Purpose: first server-boundary smoke after successful JSONL hooks.

Implemented pieces:
- `game/commandline.*`: adds `-mmo-action-udp <ipv4:port>`.
- `game/game/mmosemanticactionsink.*`: existing bounded async semantic action worker can now write local JSONL, send UDP, or both. The game thread still only snapshots/enqueues immutable envelopes.
- `tools/run_mmo_action_receiver.py`: local UDP receiver that validates required envelope fields, optionally checks session key prefix, de-duplicates by `idempotency_key`, and writes raw accepted actions to JSONL.

Important semantics:
- This is not final MMO networking. UDP may drop packets; use it only as a cheap local boundary to prove `OpenGothic client -> external receiver`.
- Do not dispatch directly from the client to MySQL. DB dispatch belongs behind the server process.
- Keep `-mmo-action-jsonl` enabled during tests so local and receiver evidence can be compared.
- Next step should map one validated receiver action to existing MySQL procedure/outbox with idempotent retry.

Recommended command pair:

```bash
python3 tools/run_mmo_action_receiver.py --bind 127.0.0.1:29777 --jsonl runtime/mmo_server_actions.jsonl --require-session local-dev-PC_HERO --truncate
```

```bash
./build/opengothic/Gothic2Notr -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" -g2   -mmo-action-jsonl runtime/mmo_actions.jsonl   -mmo-action-udp 127.0.0.1:29777   -mmo-action-session-key local-dev-PC_HERO   -mmo-action-queue-capacity 8192
```

## Receiver DB enqueue extension

The UDP receiver can now become a thin dev server boundary:

```text
UDP semantic action -> validation/dedupe -> server JSONL -> mmo_server_action_outbox
```

Important behavior:
- MySQL is used only by the separate receiver process, not by OpenGothic.
- The receiver logs in through `mmo_login_character(...)` and enqueues through `mmo_enqueue_server_action(...)`.
- `request_payload` preserves the original client envelope and adds normalized aliases for future dispatcher/resolver work.
- Current world-item/equipment envelopes are usually not dispatch-ready because they carry engine stable ids and item template ids, not MySQL item_instance UUIDs. That gap is intentional and visible through `dispatch_ready=false` and `dispatch_missing_fields`.

This extension closes the first server-boundary loop, but not the final write path:

```text
Done:    client -> receiver -> outbox
Not yet: outbox -> resolver -> mmo_* stored procedure -> journal/projection
```
