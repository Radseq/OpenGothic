# MMO server perception queue - step 173

## What changed

Added the first runtime server perception queue:

- `server/cpp/mmo_server_perception_queue.h`
- `server/cpp/mmo_server_perception_queue.cpp`

The queue stores classified perception events in memory only. It is not tied to
the temporary DB and does not yet execute NPC script reactions.

## Behavior

The queue:

- validates perception ids against the Gothic II perception catalog,
- stores source/other/victim/item keys,
- stores the server tick,
- coalesces repeated identical events in a short time window,
- keeps bounded memory with a fixed capacity,
- exposes snapshots for future reaction processing.

Diagnostics:

```text
[perception_queued]
[perception_rejected]
```

Coalesced repeated events are intentionally quiet.

## Initial event sources

The C++ server now feeds perception events from existing semantic actions:

- `ReadyWeapon` -> `PERC_DRAWWEAPON`
- `HolsterWeapon` -> `PERC_ASSESSREMOVEWEAPON`
- `PickupWorldItem` -> `PERC_ASSESSTHEFT`
- `TakeContainerItem` -> `PERC_ASSESSTHEFT`
- `RecordCombatIntent` attack/ranged attack -> `PERC_ASSESSTHREAT`
- `RecordCombatIntent` spell cast -> `PERC_ASSESSMAGIC`
- `ApplyCharacterDamage` -> `PERC_ASSESSDAMAGE`
- `ApplyWorldEntityDamage` -> `PERC_ASSESSDAMAGE` or `PERC_ASSESSMURDER`
- non-fatal `ApplyWorldEntityDamage` additionally queues
  `PERC_ASSESSOTHERSDAMAGE`

## Why

This is the first shared server memory of "what NPCs could perceive".

Before this, weapon draw, theft, combat and magic existed as separate semantic
events. The server could validate/apply some of them, but there was no common
runtime stream that a future reaction system could consume.

## Boundary

Still observe-only:

- no witness spatial query yet,
- no line-of-sight/senses evaluation yet,
- no Gothic script handler invocation yet,
- no guard/faction/attitude reaction yet,
- no NPC action interruption from perception yet.

This is deliberate. The queue is the staging layer between existing semantic
events and later server-owned reaction authority.

## Verified

Built successfully:

```text
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

Existing unrelated `slot` shadowing warnings remain in
`mmo_udp_server_payload_mapper.inl`.

## Tomorrow's next step

Build `PerceptionSensor` or `PerceptionReaction` on top of this queue.

Recommended next slice:

- consume queued events,
- find candidate witnesses by server-side spatial/interest data,
- mark whether each event has witnesses,
- do not invoke scripts yet,
- log `[perception_witnessed]` with witness count and intended reaction need.
