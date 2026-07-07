# Step150 - MMO damage calculator foundation

Goal: move the first safe slice of Gothic damage semantics into a modular C++
server component without adding new SQL procedures or views.

Client semantics checked first:
- `DamageCalculator::swordDamage` combines attacker strength, typed damage,
  victim protection, crit/hit result and Gothic 2 minimal damage.
- Gothic 2 non-critical melee damage is reduced with `(damage - 1) / 10`.
- Unarmed regular monsters in Gothic 2 are treated as critical hits.
- Ranged/magic damage is a vector of damage types minus matching protections.
- Negative protection means immunity for that damage type.
- Fall damage uses fall speed, gravity, guild fall threshold/damage and fall
  protection. It does not go through Gothic 2 minimal weapon damage.
- Bow hit chance changes by distance between reference range and max range.

Server changes:
- `mmo_server_combat_authority.h` now contains reusable damage primitives:
  `DamageVector`, `DamageActorProfile`, `DamageResult`, `DamageBounds`.
- Added pure constexpr calculations for:
  - melee damage,
  - melee min/max bounds,
  - ranged/magic vector damage,
  - ranged base damage from dexterity,
  - fall damage,
  - ranged hit chance by distance.
- Added `validateDamageProposal`, so a later direct DB path can compare a
  client-proposed damage amount against server-calculated bounds.

Design notes:
- This step intentionally does not add SQL. The calculator is data-in/data-out.
- It does not yet decide server RNG for crit/hit chance. Instead, it supports
  explicit critical input and min/max bounds so the next step can validate
  proposals safely.
- Reading combat profiles from `character_stats`, `world_entity_state`,
  equipped item templates and runtime `raw_stats` remains a separate resolver
  module.
- Applying damage still uses the existing procedures; this step prepares the
  authority logic that will sit before those calls.
