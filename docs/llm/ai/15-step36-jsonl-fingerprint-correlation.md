# 15 Step36 JSONL Fingerprint Correlation

Step 36 v1 produced a valid DB projection evidence artifact, but it warned that client/server JSONL had no matching session-prefix idempotency keys.

Observed reason:
- The replay tool can rewrite the dev session prefix in `idempotency_key` so the receiver/outbox rows are keyed under a new session such as `local-dev-PC_HERO_STEP35V2`.
- The original client JSONL may still contain the original session key.
- A strict prefix-only JSONL comparison therefore reports missing capture evidence even when the same gameplay actions are present.

Fix:
- `tools/check_mmo_step36_vertical_slice.py` now computes a Step36-only action fingerprint:

```text
action_kind + target_key
```

Examples:

```text
pickup_world_item|world-item:newworld.zen:pid:67:sym:6765
equip_character_item|item-template:6765:equip:0:1
unequip_character_item|item-template:6765:unequip:0:1
```

This fingerprint is only an evidence correlator. It is not a production idempotency key and must not replace server-side idempotency.

Behavior:
- The checker still reports exact session-prefix JSONL rows.
- If exact rows are missing but fingerprint rows match applied outbox rows, the checker reports this as a warning with a useful explanation instead of pretending the capture is absent.
- `--require-jsonl-correlation` can make missing fingerprint correlation a hard failure.

Recommended command:

```bash
python3 tools/check_mmo_step36_vertical_slice.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP35V2 \
  --client-jsonl runtime/mmo_actions.jsonl \
  --server-jsonl runtime/mmo_server_actions_step35v2.jsonl \
  --sqlite runtime/g2notr.sqlite \
  --require-two-pickups \
  --require-jsonl-correlation \
  --output runtime/mmo_step36_vertical_slice_STEP35V2.json
```

Expected after a replay-prefix run:

```text
client_jsonl: rows=4 session_rows=0 fingerprint_rows=4
server_jsonl: rows=4 session_rows=4 fingerprint_rows=4
```

If `session_rows=0` and `fingerprint_rows=4` for client JSONL, that is acceptable for replayed evidence: it means the original client capture existed under an older dev session key but semantically matches the applied server actions.
