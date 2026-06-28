# 16 Step36 Server JSONL Recovery

Purpose: make Step36 evidence robust when the receiver JSONL file is missing, empty or was truncated after the receiver accepted actions.

Observed case:
- DB/outbox/journal/projection evidence passed for `pickup_world_item`, `equip_character_item`, `unequip_character_item`.
- Client JSONL correlated by action fingerprint after replay session-key rewrite.
- Receiver JSONL path existed but had zero rows, so strict JSONL correlation failed even though the receiver had already preserved accepted envelopes in `mmo_server_action_outbox.request_payload`.

Fix:
- `tools/check_mmo_step36_vertical_slice.py` now supports `--recover-server-jsonl-from-outbox`.
- When enabled, the checker reconstructs JSONL-like server evidence from applied outbox rows for the selected session key.
- It prefers preserved original client/server envelope fields inside `request_payload` such as `client_payload`, `client_envelope`, `client_action`, `envelope` or `raw_action`.
- If a preserved envelope is unavailable, it synthesizes a minimal evidence envelope from outbox columns. Synthetic rows are only Step36 correlation evidence, not replay input and not production protocol.
- Optional `--write-recovered-server-jsonl <path>` writes the recovered evidence as JSONL for inspection.

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
  --recover-server-jsonl-from-outbox \
  --write-recovered-server-jsonl runtime/mmo_server_actions_step35v2.recovered.jsonl \
  --output runtime/mmo_step36_vertical_slice_STEP35V2.json
```

Expected output:

```text
client_jsonl: rows=4 session_rows=0 fingerprint_rows=4
server_jsonl: rows=4 session_rows=4 fingerprint_rows=4 source=outbox_request_payload recovered=1
[OK]
```

Interpretation:
- This upgrades the evidence chain from DB-only to client JSONL + recovered server acceptance + outbox + journal + projection.
- It still does not mark full native `.sav` + SQLite + MySQL restore parity as globally passed.
- A future clean run should keep receiver JSONL non-empty directly; recovery is a practical guard for local dev evidence loss.
