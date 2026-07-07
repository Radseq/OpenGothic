# MMO server observed fight intent validation - step 160

## What changed

The server now runs soft intent validation during `RecordNpcFightState`, not only
when damage arrives.

Added to `mmo_server_fight_intent_authority.*`:

- `observedAction(...)`

This maps observed combat state to a proposed execution action:

- `BS_PARADE` -> `block`
- `attack_anim`, `prehit`, `attack`, `attack_anim` state -> `attack`
- everything else -> `none`

## Integration

After `gCombatTimelineRegistry.applyObservedFight(...)` accepts a snapshot,
`mmo_udp_server_direct_world_state_apply.inl` now evaluates the observed action
with:

```text
FightIntent::evaluate(...)
```

Suspicious observed actions log:

```text
[combat_observed_intent_soft_reject]
```

This catches bad attack/block state earlier than the damage path.

## Why

Damage is too late for the final MMO server. A server-authoritative combat
pipeline needs stages:

1. proposed action,
2. intent validation,
3. accepted animation/timeline,
4. hit window,
5. damage calculation,
6. outcome/down/death.

This step adds a soft version of stage 2 for observed combat state.

## Boundary

Still soft-only. The current client can be out of sync, and the server still
lacks server-owned animation/Fight.dat imports and exact transform snapshots.

Do not hard reject from this path yet.

## Verified

Built successfully:

```text
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Start capturing explicit player/NPC combat action proposals before animation:

- player attack-left/right/front/block input,
- NPC chosen `FightMove::Action`,
- action local sequence,
- predicted animation key,
- target key.

Then route those proposals through `FightIntent::evaluate` before accepting the
timeline start.
