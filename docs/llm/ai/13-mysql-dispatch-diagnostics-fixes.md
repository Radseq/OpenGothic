# 13 MySQL Dispatch Diagnostics Fixes

Step 35 v2.4 fixes the resolver inspector after the first real pickup dispatch fixture.

Observed failure:

```text
IndexError: list index out of range
```

Cause:
- `mysql --batch --raw --skip-column-names` output can end with empty diagnostic columns.
- `stdout.strip()` removes the final tab/newline boundary, so the last row may have fewer split fields even though the SQL projection is correct.

Fix:
- `tools/inspect_mmo_action_resolution.py` now pads outbox rows to the expected projection width before reading optional `last_error_code` / `last_error_message` fields.
- This is diagnostic-only; it does not change DB state, worker dispatch, receiver behavior or C++ hooks.

Current meaningful state after the user's fixture run:
- world-item candidates are active again;
- item-instance candidates are active/world_entity again;
- `next_free_bag_index=5` proves the previous duplicate bag-index failure was caused by trying to insert into occupied slot `0`;
- the next worker run should use `--reset-matching-failed` and the session key filter, then dispatch pickup again with server-selected free bag indexes.

Continue with:

```bash
python3 tools/run_mmo_resolved_action_worker.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --worker-id dev-resolved-worker \
  --session-key local-dev-PC_HERO_STEP35V2 \
  --reset-matching-failed \
  --max-actions 10
```
