# 21 Step37 Dialog Availability Fix

## Problem

The first real Step37 MySQL E2E replay reached the receiver and outbox but failed inside the resolved worker on `set_known_dialog`:

```text
ERROR 1644 (45000): invalid dialog availability state
```

The worker called `mmo_set_character_known_dialog(...)` with the diagnostic reason string as the `availability_state` parameter.

## Correct contract

`mmo_set_character_known_dialog(...)` expects:

```text
session_id, npc_key, info_key, known, permanent, availability_state, tick, metadata, idempotency_key, out_event_id
```

The `availability_state` is derived from the Gothic dialog semantics:

```text
known=true,  permanent=false -> consumed_hidden
known=true,  permanent=true  -> repeatable_known
known=false, permanent=true  -> repeatable_not_seen
known=false, permanent=false -> one_shot_not_seen
```

OpenGothic Step37 payloads may contain `removed`. In that case `removed=true` means a one-shot consumed dialog choice, so the worker maps it to `permanent=false`.

## Fix

`tools/run_mmo_resolved_action_worker.py` now:

- normalizes `set_known_dialog` payloads to `known`, `permanent`, `removed`, and `availability_state`;
- validates explicit `availability_state` if provided;
- keeps the diagnostic `reason` only in metadata/result payload;
- normalizes numeric quest status payloads before calling `mmo_update_character_quest(...)`.

## Retry

Rerun the same session with:

```bash
--reset-matching-failed
```

or use a fresh `--session-key`. Without this, MySQL idempotency keeps the earlier failed outbox rows under the same prefix.
