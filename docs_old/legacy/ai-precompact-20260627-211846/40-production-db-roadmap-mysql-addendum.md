# Production MMO Database Roadmap - MySQL Addendum

The MySQL path mirrors the production ownership model while using MySQL 8.0 primitives:
InnoDB, `BINARY(16)` UUIDs, `JSON`, `ON DUPLICATE KEY UPDATE`, stored procedures and explicit audit tables.

## Implemented MySQL steps

1. `001_gothic_mmo_production_schema.sql` freezes account/content/realm/character/world/event-journal schema.
2. `002_bootstrap_import_pipeline.sql` imports SQLite `mmo_*_current` and baseline projections.
3. `003_server_write_path.sql` adds login/checkpoint/logout.
4. `004_wallet_write_path.sql` adds wallet/gold mutations.
5. `005_world_item_write_path.sql` adds loose world-item pickup/removal.
6. `006_character_inventory_equipment_write_path.sql` adds inventory/equipment transfer/equip/unequip.
7. `007_container_interactive_write_path.sql` adds container and interactive state mutations.
8. `008_character_progress_write_path.sql` adds quest/dialog/script/progression mutations.
9. `009_npc_lifecycle_write_path.sql` adds NPC death/respawn mutations.
10. `010_projection_validation.sql` adds basic projection validation.
11. `011_trade_economy_write_path.sql` adds NPC trade buy/sell.
12. `012_combat_resource_write_path.sql` adds character/world-entity damage, mana and item consumption.
13. `013_item_stack_write_path.sql` adds explicit stack split/merge contract.
14. `014_projection_diagnostics.sql` adds extended projection diagnostics and latest-error views.
15. `015_server_action_outbox.sql` adds the DB-side RPC/action outbox for C++ semantic hook integration.
16. `016_restore_parity_gate.sql` adds parity scenario registry and run/result gate.
17. `017_event_replay_contract.sql` adds event replay contract coverage checks.
18. `018_mmo_readiness_dashboard.sql` adds a high-level production readiness dashboard.
19. `019_server_action_dispatch_contract.sql` adds action-kind to procedure/event/projection dispatch registry plus claim/requeue procedures.
20. `020_server_action_worker_observability.sql` adds worker run/result telemetry for the outbox dispatcher.
21. `021_strict_replay_journal_audit.sql` adds stricter replay pre-flight checks.
22. `022_restore_parity_artifacts.sql` adds artifact hashes for native `.sav`, SQLite and MySQL parity proof.
23. `023_database_completion_registry.sql` adds final DB/MMO completion requirements and run/result tables.
24. `024_projection_hash_manifest.sql` adds canonical projection component hashes.
25. `025_final_database_integrity_audit.sql` adds the final DB-layer invariant audit.
26. `026_db_restore_manifest_gate.sql` adds DB-only restore manifest creation and gate status.
27. `027_database_ops_backup_manifest.sql` adds backup/export manifests and retention policy registry.
28. `028_final_read_models.sql` adds final admin/server read models and dashboards.
29. `029_external_integration_gates.sql` adds explicit external gates for C++ hooks, production worker, replay runner, parity and server authority.
30. `030_database_completion_evaluator.sql` adds the final DB completion evaluator.

## Current hard rule

Gameplay mutations must not be inferred from periodic full-world diffs once the server owns writes.
A successful operation must append one semantic event and update the current-state projection in the same transaction/procedure.

## Meaning of Step 30

`mmo_evaluate_database_completion(...)` may return:

- `database_status='complete'`: the MySQL database contract, write-path registry, replay pre-flight, DB restore manifest and DB-side operational metadata are complete enough for the next phase.
- `mmo_status='blocked'`: expected until real C++ hooks, automated restore parity and server-authority/network code exist.

Do not mark external gates as passed until real evidence exists. The database can be complete while full MMO readiness remains blocked.

## Remaining work after Step 30

1. Insert real C++ hooks at the mutation boundaries listed in `19-semantic-event-hooks.md`.
2. Replace the dev mysql-cli worker with a production RPC/server worker.
3. Implement deterministic replay that rebuilds clean projections from `content baseline + world_event_journal`.
4. Automate native `.sav` + SQLite save-slot + MySQL parity runs for all required scenarios.
5. Build server authority/network layer: movement validation, combat validation, interest management, replication, reconnect/session recovery and shard orchestration.
