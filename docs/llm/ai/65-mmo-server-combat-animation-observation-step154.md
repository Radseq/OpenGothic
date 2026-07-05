# MMO server combat animation observation - step 154

## What changed

Extended the combat observation bridge with concrete animation timing data.

Client-side read-only observation API:

- `game/graphics/mesh/pose.h`
- `game/graphics/mesh/pose.cpp`
- `game/world/objects/npc.h`
- `game/world/objects/npc.cpp`

`Pose` exposes the active primary animation, active attack animation and elapsed
time. `Npc` exposes narrow public getters so MMO hooks do not reach into private
visual internals.

`RecordNpcFightState` now sends:

- `combo_index`
- `animation_name`
- `attack_animation_name`
- `animation_elapsed_ms`
- `attack_animation_elapsed_ms`
- `animation_total_ms`
- `attack_total_ms`

Server-side timeline storage:

- `server/cpp/mmo_server_combat_timeline_authority.h`
- `server/cpp/mmo_server_combat_timeline_authority.cpp`
- `server/cpp/mmo_udp_server_direct_world_state_apply.inl`

The server parses and stores these fields in `CombatTimeline::Snapshot`.

## Why

Gothic combat validity depends on animation timing, not just high-level intent.
The client audit showed:

- damage is committed around animation `OPTIMAL_FRAME`,
- `prehit` is the attack windup before the optimal frame,
- blocking depends on defensive animation windows,
- combo continuation depends on animation timing.

The server needs the same timing vocabulary before it can safely become
authoritative for hit acceptance, block windows and combo windows.

## Boundary

This is still an observation layer, not final combat authority.

The client supplies timing facts so the server can log, correlate and build a
profile of real Gothic behavior. The final server must derive these timings from
imported animation/script data, not trust the client as the source of truth.

## Durable rule reinforced

When moving gameplay logic to the server, inspect the client and script behavior
first. Combat timing must come from Gothic animation semantics such as MDS tags
and existing client `Pose`/`FightAlgo`/`DamageCalculator` behavior, adjusted for
server architecture.

Do not replace this with a new MMO-style interpretation of swings.

## Verified

Built successfully:

```text
cmake --build build --target Gothic2Notr --config Debug
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Create a server-side combat animation profile module/importer that can represent
the timing data currently known by client animations:

- animation key,
- attack animation key,
- total time,
- `OPTIMAL_FRAME`,
- `HIT_END`,
- `PARRY_FRAME`,
- `COMBO_WINDOW`,
- weapon state,
- body state.

After that, the server can predict legal hit/parry/combo windows from an
accepted attack start instead of only observing them after the client reports
state.
