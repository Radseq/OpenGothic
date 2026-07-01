# 18 Tools Cleanup and Step37 Script/XP Slice

Purpose: keep the active tool chain aligned with the server-first direction and start Step37 without returning to `client -> MySQL`.

## Tools cleanup

Use:

```bash
python3 tools/cleanup_mmo_tools.py --root . --manifest runtime/mmo_tools_cleanup_step37.dry_run.json
python3 tools/cleanup_mmo_tools.py --root . --apply --manifest runtime/mmo_tools_cleanup_step37.json
```

Default behavior archives obsolete files into `docs/llm/legacy/tools-cleanup-step37/` instead of deleting them. Use `--delete` only when you intentionally want a destructive cleanup.

Current archive candidates:

- `tools/import_runtime_sqlite_to_postgres.py`
- `tools/check_postgres_bootstrap_import.py`
- `tools/check_postgres_mmo_schema.py`
- `tools/apply_mmo_hook_cmake_fix.py`
- `tools/compact_llm_docs.py`
- `tools/print_mysql_mmo_remaining_work.py`

Do not remove the MySQL validators, receiver, resolved worker, replay helper, Step36 evidence checker/package tool, or Step37 checker. These are still part of the active proof chain.

## Step37 target

Step37 tests this production semantic unit:

```text
bookstand/bookshelf interaction
  -> script one-shot flag, e.g. READ_BOOKSTAND_X = 1
  -> XP/progression reward exactly once
  -> world_event_journal + projections remain idempotent on retry
```

The proof must show at least:

- one applied `set_script_int` outbox action;
- one applied `adjust_progression` or `apply_experience_reward` outbox action;
- one `character_script_int_set` journal event;
- one `character_progression_adjusted` journal event;
- `character_script_state` contains the script flag projection;
- duplicate/replayed idempotency keys do not apply XP twice.

Optional Step37 evidence can also include:

- `update_quest` / `character_quest_updated` when the script changes a quest log;
- `set_known_dialog` / `character_dialog_known_set` when the bookstand/dialog path consumes a known info row;
- runtime SQLite script/global/history table counts and hash;
- client and server JSONL correlation by action fingerprint.

## Updated server-boundary tools

`tools/run_mmo_action_receiver.py` now normalizes resolver-ready payloads for:

- `set_script_int`
- `adjust_progression`
- `apply_experience_reward`
- `update_quest`
- `set_known_dialog`

`tools/run_mmo_resolved_action_worker.py` now dispatches these action kinds to existing MySQL procedures:

- `mmo_set_character_script_int`
- `mmo_adjust_character_progression`
- `mmo_apply_character_experience_reward`
- `mmo_update_character_quest`
- `mmo_set_character_known_dialog`

This remains a dev server worker. The client still only emits semantic envelopes. It does not call MySQL.

## Step37 checker

Run after a Step37 action capture/dispatch session:

```bash
python3 tools/check_mmo_step37_bookstand_script_xp.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP37_BOOKSTAND \
  --client-jsonl runtime/mmo_actions_step37_bookstand.jsonl \
  --server-jsonl runtime/mmo_server_actions_step37_bookstand.jsonl \
  --sqlite runtime/g2notr.sqlite \
  --require-jsonl-correlation \
  --output runtime/mmo_step37_bookstand_script_xp.json
```

Expected interpretation:

```text
status=passed
```

means only Step37 vertical-slice evidence passed. It is not full `.sav + SQLite + MySQL` restore parity.

## Next C++ hook work

The remaining C++ work is to place post-success script/progression hooks around the real Daedalus mutation boundary, not around UI presentation.

Likely surfaces:

- `GameScript::exec(...)` for dialog/bookstand scripts;
- `GameScript::invokeState(...)` and `GameScript::invokeItem(...)` for Mobsi/item-use scripts;
- script global writes around Daedalus global INT changes;
- player stat/progression deltas around XP/LP changes.

The hook must snapshot pre/post values, emit only after script execution succeeds, and only for live player gameplay, not during bootstrap/load/restore. It must emit one semantic action for the script one-shot flag and one semantic action for XP/progression. If the same bookstand is triggered again, the idempotency/procedure path must not grant XP twice.
