# MMO client damage roll observation - step 173

This step moves the client damage observation one layer deeper, from the later HP delta hook toward the actual `DamageCalculator` result.

## What changed

- `DamageCalculator::Val` now keeps non-authoritative runtime metadata:
  - damage kind,
  - damage modifier,
  - melee talent roll and random roll,
  - ranged projectile distance, weapon chance and random hit roll,
  - Gothic 1 projectile critical result,
  - explicit damage vector for magic/projectile paths,
  - fall speed.
- `Npc` stores the last `DamageCalculator::Val` only while applying the related HP change.
- MMO damage payloads append the roll fields already understood by the server parser:
  - `melee_random_roll`,
  - `melee_talent_chance`,
  - `projectile_distance`,
  - `projectile_weapon_chance`,
  - `projectile_random_hit_roll`,
  - `projectile_critical_hit`,
  - `projectile_spell`,
  - `damage_vector_*`,
  - `damage_modifier`,
  - `fall_speed`,
  - `fall_gravity`,
  - `fall_height_threshold`,
  - `fall_damage_per_meter`.

## Boundary

This is still observation, not hard authority. The server can now compare exact damage outcomes when these fields are present, but it still logs mismatches instead of rejecting client damage.

No SQL views, functions or procedures were added.
