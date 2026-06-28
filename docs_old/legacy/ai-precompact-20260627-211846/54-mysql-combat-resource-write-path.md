# MySQL Combat/Resource Write Path

Migration: `db/migrations/mysql/production/012_combat_resource_write_path.sql`.

Adds:

- `mmo_apply_character_damage(...)`
- `mmo_apply_world_entity_damage(...)`
- `mmo_consume_character_mana(...)`
- `mmo_consume_character_item(...)`
- `combat_resource_audit`

Scope is intentionally limited to durable gameplay consequences:

```text
damage -> health projection
spell/resource -> mana projection
ammo/consumable -> inventory/item_instance projection
```

It does not persist animation frame, projectile object lifetime, fight queue or local target heuristics.
Those remain transient; the DB stores the accepted result.
