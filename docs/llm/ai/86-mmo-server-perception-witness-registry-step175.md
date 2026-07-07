# MMO server perception witness registry - step 175

## What changed

Added the first runtime witness registry:

- `server/cpp/mmo_server_perception_witness_registry.h`
- `server/cpp/mmo_server_perception_witness_registry.cpp`

The registry stores recent observed NPC positions, facing direction and basic
state needed by `PerceptionSensor`.

## Observation sources

The server updates the registry from existing NPC observation packets:

- `RecordNpcPathState`
  - position from path command,
  - optional yaw from `rotation_yaw` or `yaw` payload fields.
- `RecordNpcFightState`
  - position from `attacker_center`,
  - yaw from `attacker_yaw_rad`,
  - down/dead/unconscious state.

Observations expire after a short runtime TTL. They are not persisted.

## Queue integration

After a new perception event is queued, the server now evaluates it against
recent observed NPC candidates and logs:

```text
[perception_witnessed] seq=<id> perc=<event> comparable=<n> witnessed=<n>
```

This creates the first server-owned answer to:

```text
Could anyone have witnessed this event?
```

## Boundary

Still observe-only:

- no script handler invocation yet,
- no guard/faction/attitude change yet,
- no line-of-sight raycast yet,
- no imported `percRanges()` per perception yet,
- no exact per-NPC perception function table yet,
- no hard rejection of crimes or combat based on witnesses yet.

This step deliberately stops at witness counting. That keeps the migration
modular and avoids inventing guard behavior before checking Gothic II scripts.

## Verified

Built successfully:

```text
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

Existing unrelated `slot` shadowing warnings remain in
`mmo_udp_server_payload_mapper.inl`.

## Next good step

Add a `PerceptionReactionPlanner` that consumes witnessed events and only
classifies intended reaction:

- ignore,
- warn,
- interrupt current action,
- call help,
- start combat,
- mark suspected crime.

Do not invoke Gothic scripts yet. First make the reaction plan visible in logs
and make sure it agrees with the Gothic II perception/script paths.
