# MMO server fight intent authority - step 159

## What changed

Added the first soft server-side combat intent authority layer:

- `server/cpp/mmo_server_fight_intent_authority.h`
- `server/cpp/mmo_server_fight_intent_authority.cpp`

This module evaluates proposed combat execution actions against:

- the last combat timeline snapshot,
- Gothic-style spatial/focus validation,
- `FightMove::validateExecution`.

## Current integration

`ApplyWorldEntityDamage` now treats incoming melee damage as evidence that the
attacker proposed an `attack` intent.

Before applying the DB damage bridge, the server evaluates:

```text
FightIntent::evaluate(action=attack, attacker, target, timeline)
```

If the intent is suspicious, the server logs:

```text
[combat_intent_soft_reject]
```

The damage is still applied after logging.

## Why soft only

Hard rejection is intentionally not enabled yet.

The server still lacks:

- server-owned MDS animation timing import,
- server-owned Fight.dat import,
- deterministic server combat RNG,
- authoritative attacker/target transform snapshots for exact action ticks,
- `DamageCalculator` parity,
- a live ACK/NACK path that rolls back client prediction cleanly.

Until those pieces exist, this layer is a production-shaped validator and audit
surface, not final anti-cheat authority.

## Client parity notes

The attack validation mirrors client execution rules from `Npc::implFightAi`:

- attack needs a narrow focus angle before it can proceed directly,
- side attacks outside W-range are consumed/skipped,
- block requires focus and a melee-capable weapon,
- jumpback has parade/swim/focus constraints.

The narrow attack focus uses the 5-degree check seen in the client attack path.

## Verified

Built successfully:

```text
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Start feeding real combat intents into this module before damage:

- player attack button/swing proposals,
- NPC chosen `FightMove::Action`,
- block/parry proposals,
- jumpback proposals,
- ranged/magic proposals separated from melee.

Once transform snapshots, MDS import and `DamageCalculator` parity are in place,
the same module can move from soft logging to hard ACK/NACK.
