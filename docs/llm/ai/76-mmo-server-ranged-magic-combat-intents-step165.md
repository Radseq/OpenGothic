# MMO server ranged and magic combat intents - step 165

## What changed

Added explicit combat intent names for non-melee combat:

- `shoot_ranged`
- `cast_spell`

The server fight move model now recognizes both actions instead of collapsing
them to `none`.

## Client observation points

Ranged combat uses the existing client path:

```text
Npc::shootBow
```

The intent is emitted after ammunition is confirmed and before the attack
animation is requested:

```text
shoot_ranged / proposed
```

It is accepted after the projectile exists, ammunition is consumed and the
bullet damage/chance fields are initialized:

```text
shoot_ranged / accepted
```

Magic combat uses the existing client path:

```text
Npc::beginCastSpell
```

The intent is emitted after basic cast preconditions and before
`Spell_ProcessMana` decides the result:

```text
cast_spell / proposed
```

It is accepted for the existing Gothic spell outcomes that start invest/cast:

- `SPL_STATUS_CANINVEST_NO_MANADEC`
- `SPL_RECEIVEINVEST`
- `SPL_NEXTLEVEL`
- `SPL_SENDCAST`

It is rejected for mana-process outcomes that explicitly refuse the cast.

## Server validation

Soft validation now knows:

- `shoot_ranged` requires `bow` or `crossbow` weapon mode,
- `shoot_ranged` requires focus,
- `cast_spell` requires `mage` weapon mode.

This is intentionally minimal. Projectile trajectory, ammo authority, spell
mana cost, spell level, cast release and final effect authority are separate
server modules to build later.

## Why

Melee intent correlation was no longer enough. Gothic combat also has ranged
and magic paths that can produce damage/effects without passing through melee
`swingSword` functions.

This step keeps the same rule as the rest of the server migration: observe the
current client decision points first, then move those decisions to server-owned
modules with the same semantics.

## Boundary

Still soft-authoritative:

- no hard ACK/NACK,
- no rollback,
- no server-owned projectile spawning yet,
- no server-owned spell mana/effect commit yet.

## Verified

Built successfully:

```text
cmake --build build --target Gothic2Notr --config Debug
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

The server build still reports existing `slot` shadowing warnings in
`mmo_udp_server_payload_mapper.inl`; they are unrelated to this step.

## Next good step

Split ranged and magic into dedicated authority modules:

- ranged authority: ammo, projectile spawn, hit chance, crit chance,
- magic authority: mana process, invest/cast/release, spell effect commit.
