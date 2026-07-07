# MMO server combat range model - step 157

## What changed

Added a narrow server-side model for Gothic melee attack distance:

- `server/cpp/mmo_server_combat_range_model.h`
- `server/cpp/mmo_server_combat_range_model.cpp`

The model represents the client `FightAlgo` formula:

```text
preferred attack distance = attacker guild base + target guild base + weapon/fist range
```

This is the same semantic direction as `FightAlgo::prefferedAttackDistance`.

## Client observation bridge

`RecordNpcFightState` now sends range components, not only the final distance:

- `attacker_fight_range_base`
- `opponent_fight_range_base`
- `weapon_range`
- existing `attack_range`

The values still come from the client today, using Gothic guild values and the
active weapon/fist range. They are observation/evidence until the server imports
content data itself.

## Server integration

`mmo_server_combat_spatial_authority.*` now prefers the model-derived range when
all components are present:

```text
attacker_fight_range_base + opponent_fight_range_base + weapon_range
```

It falls back to the old `attack_range` payload field, then to a conservative
default, so old packets keep working.

`mmo_server_combat_timeline_authority.*` stores and validates the range
components in the runtime combat snapshot.

## Boundary

This still does not make combat fully server-authoritative. The server can now
evaluate melee hit plausibility using Gothic-shaped range data, but the final
authority path still needs:

- server-side import of guild fight ranges,
- server-side item/weapon length data,
- authoritative attacker and target transforms at the attack tick,
- `FightAlgo` move selection/parry/evade parity,
- `DamageCalculator` parity.

## Verified

Built successfully:

```text
cmake --build build --target Gothic2Notr --config Debug
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Start `fight_move_model`:

- represent `FightAiMove` and server fight actions,
- mirror `FightAlgo::fillQueue` decision order,
- mirror `nextFromQueue` move expansion,
- keep randomness injectable/deterministic for server tick replay.
