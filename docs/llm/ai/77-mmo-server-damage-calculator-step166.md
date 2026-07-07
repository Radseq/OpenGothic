# MMO server damage calculator - step 166

## What changed

Added the first focused server-side damage calculator module:

- `server/cpp/mmo_server_damage_calculator.h`

It wraps the already ported Gothic-shaped primitives from
`mmo_server_combat_authority.h` into a dedicated combat damage audit layer for:

- melee damage bounds,
- ranged damage value,
- magic/vector damage value when an explicit damage vector is present,
- fall damage when fall input is present.

No SQL procedure, view or DB schema change was added.

## Client observation payload

`ApplyCharacterDamage` and `ApplyWorldEntityDamage` payloads now include a
compact combat profile when the data is available:

- source and target strength/dexterity,
- source damage type mask,
- source damage vector,
- target protection vector,
- source hit chance array,
- monster and active-weapon flags,
- inferred damage kind: `melee`, `ranged`, `magic` or `unknown`,
- Gothic game version.

This data comes from the same client `Npc`/weapon state that the existing
`DamageCalculator` uses. The old single-player behavior is unchanged; these
fields are emitted only through the existing MMO semantic action path.

## Server integration

Before applying the existing direct DB damage bridge, the C++ server evaluates
the observed damage against the new module.

If the payload is comparable and the value is outside the Gothic-shaped result,
the server logs:

```text
[combat_damage_calculator_mismatch]
```

The damage is still applied. This is intentionally soft authority until the
server owns:

- deterministic combat RNG,
- projectile spawn and hit authority,
- spell mana/invest/cast/release authority,
- authoritative transform snapshots at hit tick,
- client rollback/correction for combat prediction.

## Boundary

This step does not make combat fully server-authoritative yet.

It moves damage validation into a modular C++ runtime layer and gives the server
the vocabulary needed to compare client-observed melee/ranged/magic/fall damage
against Gothic semantics without adding more database coupling.

## Next good step

Split ranged and magic into dedicated authority modules:

- ranged authority: ammo, projectile spawn, hit chance and critical chance;
- magic authority: mana process, invest/cast/release and spell effect commit.

After those modules exist, `DamageCalculator` can become a hard gate for
accepted damage instead of a soft mismatch log.
