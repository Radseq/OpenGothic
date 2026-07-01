# 00 Core Context

Goal: turn OpenGothic Gothic II NotR into a server-authoritative MMO without losing native save/load parity before DB restore is proven.

Hard rules:
- Use C++23. Performance matters first. Avoid runtime reflection-heavy or allocation-heavy designs on hot gameplay paths.
- Preserve normal Gothic `.sav` until DB restore parity is green for required scenarios. `.sav` is a compatibility path, backup and oracle, not the final MMO state model.
- Runtime SQLite is a local bridge/capture/restore validator, not the production server DB.
- MySQL 8.0+ is the current production-shaped DB target. It already has migrations 001..030 applied in the active path.
- Do not infer authoritative gameplay mutations from periodic full-world diffs once server ownership begins. A successful mutation must append one semantic event and update current-state projection in the same procedure/transaction.
- Never mark external gates as passed without real evidence. `database_status=complete` and `mmo_status=blocked` is valid after Step 30.
- Treat display names as labels only. Stable identity uses engine keys: persistent id, symbol index/script id, world/template revision, VOB id, item template symbol, character key.
- Do not persist transient local state: pointers, AI queues, fight queues, render handles, animation pose/frame, active particles, audio handles, input state, focus pointers, camera presentation.

Current state after MySQL Step 30:
- DB layer is complete as a contract: account/content/realm/character/world/event journal, write procedures, audits, outbox/dispatch registry, worker telemetry, strict replay audit, restore parity artifacts, backup manifests, final dashboards.
- Full MMO remains blocked by external work: real C++ hooks, production RPC/server worker, deterministic replay runner, automated `.sav + SQLite + MySQL` parity, movement/combat/network authority, replication, reconnect and shard orchestration.

Architecture target:
```text
OpenGothic client -> MMO server -> MySQL procedures/event journal -> projections -> replication
```
Never design final flow as `client -> MySQL`. Dev-only adapters may exist to prove contracts, but production ownership belongs to the MMO server.

Design principle for future changes:
- Add thin, explicit seams at mutation boundaries.
- Prefer fixed-size or bounded queues for client-side semantic action capture.
- Keep game thread free of blocking DB/network I/O.
- Snapshot required payload on game thread, then dispatch immutable data asynchronously or through a server intent channel.
