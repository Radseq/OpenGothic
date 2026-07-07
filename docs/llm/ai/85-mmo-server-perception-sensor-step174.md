# MMO server perception sensor - step 174

## What changed

Added a server-side perception sensor module:

- `server/cpp/mmo_server_perception_sensor.h`
- `server/cpp/mmo_server_perception_sensor.cpp`

The existing runtime `PerceptionQueue` now stores an optional event origin
position. Existing semantic events feed that position when available:

- weapon draw/holster: `actor_position`,
- item pickup/container take: `actor_position`,
- combat intent/magic intent: `actor_position`,
- damage/murder/others-damage: `target_position`.

`[perception_queued]` diagnostics now include:

```text
has_pos=0|1
```

## Sensor model

The new module evaluates whether a candidate witness could perceive a queued
event.

It currently checks:

- event has an origin position,
- witness has a valid key,
- witness is not down,
- witness is not the source actor,
- finite positions,
- required sense category,
- domain-specific default range,
- vertical range,
- optional focus check for crime/room/item/movement events.

Domain defaults:

- crime: shorter witness range,
- combat: broader witness range,
- sound: hearing range and no focus requirement,
- magic: magic witness range,
- room: crime-like witness range,
- social/item/movement/command: default NPC perception range.

## Why

`PerceptionQueue` is only useful if it can become a shared server decision about
who witnessed an event.

This step deliberately does not invoke Gothic script handlers yet. It creates
the modular visibility layer between:

```text
semantic event -> perception queue -> witness filter -> reaction authority
```

## Boundary

Still not final perception authority:

- no server-side NPC candidate list is wired yet,
- no line-of-sight raycast yet,
- no script `percRanges()` import yet,
- no per-NPC perception function table yet,
- no guard/faction reaction yet.

The next step must feed real NPC candidates into this module from current server
NPC/world-state observations.

## Verified

Built successfully:

```text
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

Existing unrelated `slot` shadowing warnings remain in
`mmo_udp_server_payload_mapper.inl`.

## Next good step

Build a small runtime `PerceptionWitnessRegistry`:

- track observed NPC positions and forward vectors from world-state packets,
- on each queued perception event, evaluate nearby observed NPCs,
- log `[perception_witnessed]` with witness count,
- still do not call scripts or mutate NPC AI yet.
