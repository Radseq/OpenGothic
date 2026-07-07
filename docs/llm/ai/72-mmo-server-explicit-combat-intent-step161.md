# MMO server explicit combat intent - step 161

## What changed

Added an explicit semantic action for combat intent:

- `SemanticActionKind::RecordCombatIntent`
- action kind: `record_combat_intent`
- event type: `combat_intent_recorded`
- event class: `combat`

Client hook added:

- `Mmo::Hooks::onCombatIntent(...)`

The hook is currently emitted after successful local `doAttack(...)` calls from:

- `Npc::swingSword()` -> `attack`
- `Npc::swingSwordL()` -> `attack_left`
- `Npc::swingSwordR()` -> `attack_right`
- `Npc::blockSword()` -> `block`

## Server handling

`mmo_udp_server_direct_combat_apply.inl` handles `RecordCombatIntent` without DB
coupling.

It parses:

- `actor_key`
- `target_key`
- `combat_action`

Then routes the action through:

```text
FightIntent::evaluate(...)
```

Suspicious explicit intents log:

```text
[combat_explicit_intent_soft_reject]
```

The action is still soft-accepted.

## Why

Previous steps inferred combat intent from observed fight state or damage. That
is useful but late.

This step creates the first explicit client-to-server combat intent event before
damage is applied. It is still emitted after the local animation was accepted by
the client, but it is much closer to the eventual server-authoritative shape:

```text
input/proposal -> intent validation -> accepted timeline -> hit window -> damage
```

## Boundary

This is not final hard authority.

The hook currently reports successful local `doAttack(...)`, not a pre-animation
input proposal. The next authority step should move even earlier, toward player
input/NPC selected action proposals before local animation starts.

No DB procedure was added. Combat intent is runtime validation/audit only.

## Verified

Built successfully:

```text
cmake --build build --target Gothic2Notr --config Debug
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Move explicit combat intent earlier:

- hook player attack/block input before `doAttack`,
- hook NPC `FightMove::Action` before execution,
- include action local sequence and predicted animation key,
- use server ACK/NACK once transform snapshots and animation profiles are
  server-owned.
