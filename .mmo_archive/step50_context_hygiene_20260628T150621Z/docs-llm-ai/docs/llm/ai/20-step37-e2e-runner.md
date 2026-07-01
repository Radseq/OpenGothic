# 20 Step37 E2E Runner

Purpose: reduce Step37 server-side validation to one repeatable command after the real C++ hook emits script/progression JSONL.

New tool:

- `tools/run_mmo_step37_bookstand_mysql_e2e.py`

Inputs:

- real client JSONL from OpenGothic, usually `runtime/mmo_client_actions_step37_script_xp.jsonl`;
- MySQL URL;
- fresh e2e session/idempotency prefix;
- optional SQLite runtime DB for checker evidence.

Pipeline:

```text
OpenGothic C++ hook capture
  -> filter Step37 action kinds
  -> rewrite idempotency prefix for isolated e2e run
  -> UDP replay into run_mmo_action_receiver.py
  -> enqueue accepted rows into mmo_server_action_outbox
  -> dispatch with run_mmo_resolved_action_worker.py
  -> verify with check_mmo_step37_bookstand_script_xp.py
```

The runner requires at least:

- one `set_script_int` row;
- one `adjust_progression` or `apply_experience_reward` row;
- unique idempotency keys after rewrite;
- receiver resolver-ready payloads;
- no failed/dead-letter Step37 rows after worker dispatch;
- final Step37 checker status `passed`.

This runner intentionally remains a dev server-boundary evidence tool. It does not change the production architecture and does not allow the game process to call MySQL directly.
