# MySQL Server Action Outbox

Migration `015_server_action_outbox.sql` adds the first DB-side boundary for C++ semantic hook integration.

The table `mmo_server_action_outbox` is not a gameplay projection. It is an idempotent handoff queue between a future C++/RPC layer and the already implemented MySQL procedures. It records `action_kind`, stable `target_key`, `idempotency_key`, JSON request payload and status.

Procedures:

- `mmo_enqueue_server_action(...)`
- `mmo_mark_server_action_applied(...)`
- `mmo_mark_server_action_failed(...)`

The intended flow is:

```text
C++ mutation boundary -> semantic action envelope -> server/RPC worker -> MySQL write-path procedure -> applied/failed action state
```

Do not write action rows from UI code directly. The UI requests gameplay intent; the game/server layer validates and enqueues/executes a semantic action.
