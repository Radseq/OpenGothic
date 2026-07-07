# MMO server NPC AI explicit combat intent - step 164

## What changed

NPC melee AI now emits explicit combat intent events around its direct attack
path in `Npc::implAttack`.

The direct AI branch used `doAttack(...)` without going through:

- `Npc::swingSword`,
- `Npc::swingSwordL`,
- `Npc::swingSwordR`,
- `Npc::blockSword`.

That meant player/manual combat wrappers were visible to the server intent
pipeline, but this NPC AI path could still start a melee attack without an
explicit `RecordCombatIntent`.

## Behavior

For melee AI actions:

- `MV_ATTACK` maps to `attack`,
- `MV_ATTACKL` maps to `attack_left`,
- `MV_ATTACKR` maps to `attack_right`.

Before `doAttack(...)`, the client emits:

```text
intent_state = proposed
```

After `doAttack(...)` accepts the animation start, it emits:

```text
intent_state = accepted
```

Rejected local starts remain visible as a `proposed` event without a matching
`accepted`; the server-side correlation registry can expire and diagnose them.

## Why

This closes an important observation gap for server-authoritative combat.

The server already correlates:

```text
proposed -> accepted
```

but it only helps when all meaningful attack entry points feed it. NPC AI has a
direct combat path, so it must report the same intent lifecycle as the player
wrapper functions.

## Boundary

This is still client-observed, not final server authority.

The durable rule still applies: when moving combat decisions to the server,
inspect the existing client/Gothic path first and reproduce its semantics in a
server-owned form. This step observes the existing AI `FightAlgo` action and
`doAttack(...)` acceptance. It does not invent a new attack decision model.

## Verified

Built successfully:

```text
cmake --build build --target Gothic2Notr --config Debug
```

## Next good step

Move this one layer earlier toward a server-owned NPC combat action queue:

- record the chosen `FightAlgo::Action`,
- record whether the current target/range/focus made the action legal,
- let the server ACK/NACK the intent before final authoritative damage.
