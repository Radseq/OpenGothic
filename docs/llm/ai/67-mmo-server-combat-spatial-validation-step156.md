# MMO server combat spatial validation - step 156

## What changed

Extended combat observation and timeline validation with Gothic-style spatial
facts for melee attacks.

Client `RecordNpcFightState` now includes:

- `attacker_yaw_rad`
- `weapon_range`
- `attack_range`
- `attacker_center`
- `opponent_center`
- `fight_distance`
- `opponent_yaw_rad`

The values are derived from the same client semantics audited in `FightAlgo`:

- focus angle uses attacker rotation against target center,
- attack range uses guild fight base distance plus active melee weapon/fist
  range,
- centers use collision/fight-distance data exposed by `Npc`.

## Server module

Added:

- `server/cpp/mmo_server_combat_spatial_authority.h`
- `server/cpp/mmo_server_combat_spatial_authority.cpp`

The module evaluates:

- whether the target is inside melee range,
- whether the target is inside the Gothic 30-degree focus cone,
- distance and focus dot diagnostics.

`mmo_server_combat_timeline_authority.*` now stores spatial fields in the fight
snapshot and checks them before accepting melee damage evidence.

Mismatch reasons now include:

- `outside_fight_range`
- `outside_focus_angle`
- `before_animation_hit_window`
- `after_animation_hit_window`

## Boundary

This is still soft validation. `ApplyWorldEntityDamage` logs
`[combat_timeline_damage_mismatch]` but does not reject damage yet.

Hard rejection should wait until:

- server imports/owns animation profiles instead of receiving timing from the
  client,
- server ports `DamageCalculator`,
- server has authoritative attacker/target transform snapshots for the exact
  combat tick,
- ranged/spell/projectile damage is separated from melee damage.

## Persistence note

No DB coupling was added. Spatial combat state lives in runtime timeline memory.
This fits the rule that the current database is temporary and should not become
the final combat authority model.

## Verified

Built successfully:

```text
cmake --build build --target Gothic2Notr --config Debug
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Move from spatial validation to server-side fight move legality:

1. Create a server `fight_range_model` with imported guild values and item
   weapon length.
2. Create a server `fight_move_model` mirroring `FightAlgo::fillQueue` and
   `nextFromQueue`.
3. Let server choose/accept attack intents before animation starts.
4. Keep client attack animation as prediction/evidence until server snapshots
   can drive it.
