# MySQL Projection Diagnostics

Migration: `db/migrations/mysql/production/014_projection_diagnostics.sql`.

Adds extended projection checks and human-readable views:

```text
mmo_validate_world_projection_extended(...)
v_projection_validation_latest_errors
v_item_projection_diagnostics
v_world_entity_projection_diagnostics
```

This is meant for diagnosing cases like:

```text
projection validation run: failed/errors=1
```

The extended validator checks owner/projection invariants around:

- character inventory and item owner IDs;
- character equipment rows requiring inventory rows;
- world/container inventory item ownership;
- NPC trade inventory stock;
- dead/world entity health states;
- inactive item instances still appearing in inventory projections.

This is still not the final replay validator from `content baseline + world_event_journal`; it is a production-useful
projection invariant checker.
