# MMO server ranged authority profile - step 166

## What changed

Added a small server-side ranged combat authority module:

- `mmo_server_ranged_combat_authority.h`
- `mmo_server_ranged_combat_authority.cpp`

It is pure C++23 logic with no DB dependency and no transport dependency.

## Client parity source

This module mirrors the existing client behavior from:

- `DamageCalculator::rangeDamageValue`
- `Npc::shootBow`

Observed client rules:

- projectile damage uses active weapon damage type mask,
- Gothic 2 adds dexterity to ranged damage,
- Gothic 1 does not add dexterity to ranged damage in `rangeDamageValue`,
- Gothic 1 hit chance is `dexterity / 100`,
- Gothic 1 crit chance is bow/crossbow talent value divided by 100,
- Gothic 2 hit chance is `hitchance[TALENT_BOW]` or `hitchance[TALENT_CROSSBOW]`,
- Gothic 2 does not set projectile crit chance in this path.

## Server model

The module exposes:

- `computeDamage(...)`
- `computeHitChance(...)`
- `computeCritChance(...)`
- `validateShot(...)`
- `buildProjectileProfile(...)`

`validateShot(...)` checks:

- ranged weapon mode,
- ammunition presence,
- projectile item presence,
- focus.

## Why

The previous step made `shoot_ranged` visible as a combat intent. This step
starts moving the actual ranged combat payload toward server authority without
creating a projectile system yet.

The logic is intentionally small and replaceable. Future server projectile code
can call this module, while DB and replication can still be rewritten later.

## Remaining combat/AI server-authority work

High priority:

- hard ACK/NACK for combat intents,
- client rollback/correction for denied animation starts,
- server-owned melee hit commit at `OPTIMAL_FRAME`/`HIT_END`,
- server-owned block/parry legality at `PARRY_FRAME`,
- server-owned ranged projectile spawn and hit resolution,
- server-owned magic mana/invest/cast/release/effect commit,
- authoritative interruption of talk/sleep/routine/combat by threat.

Medium priority:

- action priority/threat scoring from attacks, theft, weapon draw and mobs,
- deterministic replication of ongoing NPC conversations/actions to late
  joining players in range,
- server-owned NPC action queue fed by Gothic `FightAlgo` semantics,
- ranged/magic state snapshots for players entering interest range,
- better diagnostics that group `proposed -> accepted -> damage/effect`.

Later:

- replace temporary DB-backed persistence with final storage boundaries,
- split replication from persistence completely,
- move more Daedalus/script decisions into server-owned adapters.

## Verified

Built successfully:

```text
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

The server build still reports existing `slot` shadowing warnings in
`mmo_udp_server_payload_mapper.inl`; unrelated to this module.

## Next good step

Feed `shoot_ranged` payloads with enough observed client data to compare the
server profile against the client projectile:

- damage type mask,
- per-type weapon damage,
- dexterity,
- bow/crossbow talent,
- bow/crossbow hit chance,
- ammo item identity.
