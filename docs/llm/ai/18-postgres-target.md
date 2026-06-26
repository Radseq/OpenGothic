# PostgreSQL MMO Target

File: `db/migrations/postgres/001_gothic_mmo_schema.sql`.

The migration is the production-shaped target, not the local SQLite runtime schema.

- Account: `account_accounts`, `account_entitlements`.
- Realm/shard: `realm_realms`, `realm_world_instances`.
- Content baseline: `content_game_targets`, `content_world_templates`, entity/item templates.
- Character: `characters`, stats, inventory, equipment, quests, dialogs, script state.
- Persistent world: entity state, inventory, script state, and event journal.

Keep SQLite capture compatible with this ownership split. Do not copy `runtime_*` table design directly into a multiplayer server.

