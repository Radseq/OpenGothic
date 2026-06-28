# 26 Step38 Stale Combat No-op Fix

Context:
- The project DB schema was shown from the real MySQL instance. `server_sessions` uses `last_seen_at`/`login_at`, not a universal `updated_at` column.
- After the fixture schema fix, Step38 `consume_item` replay passed end-to-end: three ammunition rows applied and `character_item_consumed` appeared in `world_event_journal`.
- The full combat replay then applied damage and `mark_npc_dead` for an NPC, and a later captured local damage envelope failed because the DB procedure correctly rejects damage against inactive entities.

Patch:
- `run_mmo_resolved_action_worker.py` now checks resolved NPC lifecycle before calling combat lifecycle procedures.
- If `apply_world_entity_damage` targets a non-active entity, the worker marks the outbox row applied as an explicit no-op:
  - `applied_noop=true`
  - `noop_reason=target_entity_not_active`
  - `event_emitted=false`
- If `mark_npc_dead` targets an already inactive entity, the worker also marks it applied as a no-op:
  - `noop_reason=target_entity_already_inactive`
- Real active-target damage and death still call the existing MySQL procedures and append journal events.

Meaning:
- This fixes dev replay ordering/noise after death. It does not make the client authoritative.
- Production still needs server-side intent validation before consequences are applied.
