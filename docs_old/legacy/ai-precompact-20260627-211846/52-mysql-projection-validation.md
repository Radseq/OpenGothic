# MySQL Projection Validation

Migration `010_projection_validation.sql` adds basic projection validation scaffolding.

## New tables

`mmo_projection_validation_runs` stores a validation execution against a world instance.

`mmo_projection_validation_results` stores individual consistency checks and their problem counts.

## Procedure

`mmo_validate_world_projection_basic(...)` currently checks:

- character inventory rows point to active character-owned item instances;
- equipped items also exist in the character inventory;
- world/container inventory rows point to active container-owned item instances;
- character current world and character position world agree;
- active sessions point to characters in the same current world.

It also advances `world_projection_offsets` for the `basic_projection_validator` projection name.

## Important limitation

This is not the final deterministic replay validator yet. The final gate must rebuild projections from:

```text
content baseline + world_event_journal
```

and compare rebuilt state against current projection tables. This migration is the first cheap
consistency layer that can run during development and CI-like smoke tests.
