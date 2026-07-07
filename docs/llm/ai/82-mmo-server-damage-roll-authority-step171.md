# MMO server damage roll authority - step 171

## What changed

Extended the server damage model beyond value/bounds comparison.

The server combat authority now has explicit damage outcomes:

- `NoHit`,
- `Hit`,
- `CriticalHit`,
- `Invincible`.

Added server-side roll-aware functions:

- `Combat::calculateMeleeDamageWithRoll(...)`,
- `Combat::rangedProjectileHits(...)`,
- `Combat::calculateRangedDamageWithRoll(...)`.

Added compile-time coverage in:

- `server/cpp/mmo_server_damage_calculator.cpp`.

## Client parity source

This was based on the current client `DamageCalculator`:

- melee damage uses strength, typed damage, target protection and crit roll,
- Gothic 2 non-critical melee divides damage by 10 after protection,
- Gothic 2 monsters without a weapon always count as critical melee,
- Gothic 1 critical melee uses `criticalDamageMultiplyer()`,
- ranged hit chance interpolates over distance,
- Gothic 1 ranged can critical-multiply projectile damage,
- vector damage handles ranged, magic and damage modifiers,
- fall damage uses guild fall threshold/damage and fall protection,
- negative protection means immunity for that damage type,
- Gothic 2 minimum damage is applied only after a real non-invincible hit.

## Payload improvements

Damage payloads now include:

- `critical_damage_multiplier`,
- `source_actor_combat_melee_talent_chance`.

The server parser also understands future roll fields:

- `melee_random_roll`,
- `melee_talent_chance`,
- `projectile_distance`,
- `projectile_weapon_chance`,
- `projectile_random_hit_roll`,
- `projectile_critical_hit`,
- `projectile_spell`.

If these fields are absent, the server keeps the previous soft-audit behavior:

- melee uses legal damage bounds,
- ranged/magic uses deterministic vector value when comparable,
- fall uses deterministic fall value when comparable.

If roll fields are present, the server evaluates the exact expected outcome.

## Why

The client-observed damage event is still evidence, not final truth.

Before damage can become server-authoritative, the server needs vocabulary for
the full combat result, not just an integer HP delta. This step separates:

- hit legality,
- damage roll,
- critical roll,
- immunity/protection filtering,
- final HP delta.

## Boundary

Still not hard-authoritative:

- the client does not yet submit RNG rolls from `DamageCalculator`,
- ranged projectile distance is not yet attached to damage payloads,
- magic explicit damage vectors are only comparable when present,
- server still logs mismatches instead of rejecting damage,
- no client rollback/correction exists yet.

## Verified

Built successfully:

```text
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
cmake --build build --target Gothic2Notr --config Debug
```

Known unrelated warnings remain:

- `slot` shadowing in `mmo_udp_server_payload_mapper.inl`,
- existing client warnings in `camera.cpp`, `gamemusic.cpp` and mesh code.

## Next good step

Move the client observation one layer deeper into `DamageCalculator`:

- record the actual melee crit roll,
- record the actual ranged hit roll,
- record ranged distance/path length,
- record Gothic 1 ranged critical roll,
- record explicit magic damage vectors and spell category.

After that, the server can compare exact outcomes instead of broad bounds and
can start rejecting impossible damage.
