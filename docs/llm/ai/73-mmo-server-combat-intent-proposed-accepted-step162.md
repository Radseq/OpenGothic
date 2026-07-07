# MMO server combat intent proposed/accepted - step 162

## What changed

`RecordCombatIntent` now distinguishes two stages:

- `proposed` - emitted before local `doAttack(...)` is attempted,
- `accepted` - emitted after local `doAttack(...)` succeeds.

Client calls now emit both stages for:

- `Npc::swingSword()` -> `attack`
- `Npc::swingSwordL()` -> `attack_left`
- `Npc::swingSwordR()` -> `attack_right`
- `Npc::blockSword()` -> `block`

The payload includes:

- `combat_action`
- `intent_state`
- actor/target identity,
- weapon/body state,
- current animation names,
- actor/target positions and collision centers.

## Server handling

`RecordCombatIntent` still routes through:

```text
FightIntent::evaluate(...)
```

Soft reject logs now include the state:

```text
[combat_explicit_intent_soft_reject] ... state=proposed|accepted
```

## Why

This creates the first explicit proposal/acceptance sequence in the combat
pipeline. It is still client-observed, but the shape now matches the future
server-authoritative flow:

```text
proposed action -> server validation -> accepted action -> timeline -> damage
```

## Boundary

The proposal is still emitted inside client `Npc` methods, immediately before
local `doAttack(...)`. The final version should capture player input and NPC
AI-selected actions even earlier, then wait for server ACK before committing the
authoritative animation/timeline.

## Verified

Built successfully:

```text
cmake --build build --target Gothic2Notr --config Debug
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Add a small runtime sequence/registry for combat intents:

- remember last `proposed` action per actor,
- correlate following `accepted` action,
- log accepted-without-proposed and proposed-without-accepted,
- later convert this to ACK/NACK state.
