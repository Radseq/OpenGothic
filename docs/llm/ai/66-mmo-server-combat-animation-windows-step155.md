# MMO server combat animation windows - step 155

## What changed

Added explicit combat animation timing fields to the client observation payload
and server timeline.

Client-side Gothic-derived timings now come from the same animation data used by
the normal client combat code:

- `OPTIMAL_FRAME`
- `HIT_END`
- `PARRY_FRAME`
- `COMBO_WINDOW`

Touched client files:

- `game/graphics/mesh/animation.h`
- `game/graphics/mesh/animation.cpp`
- `game/graphics/mesh/pose.h`
- `game/graphics/mesh/pose.cpp`
- `game/world/objects/npc.h`
- `game/world/objects/npc.cpp`
- `game/game/mmosemantichooks.cpp`

`RecordNpcFightState` now also sends:

- `attack_optimal_ms`
- `attack_hit_end_ms`
- `parry_window_start_ms`
- `parry_window_end_ms`
- `combo_window_start_ms`
- `combo_window_end_ms`

## Server model

Added:

- `server/cpp/mmo_server_combat_animation_profile.h`
- `server/cpp/mmo_server_combat_animation_profile.cpp`

This module interprets animation timing as combat windows:

- before `OPTIMAL_FRAME` => windup/prehit,
- around `OPTIMAL_FRAME` through `HIT_END` => legal hit window,
- after hit end => recovery,
- `PARRY_FRAME` => legal defence/parry window,
- `COMBO_WINDOW` => legal combo continuation window.

`mmo_server_combat_timeline_authority.*` now stores these fields and uses them
before falling back to the older broad observed-attack grace window.

## Important boundary

This is still a bridge, not the final importer.

The timings are reported by the client today so the server can validate and
correlate damage using real Gothic semantics. The production server should later
import MDS/model-script timing data itself and treat the client timing only as
debug/evidence.

## Build note

The server build reconfigured CMake and exposed an unrelated duplicate-symbol
problem in the temporary persistence split: functions existed both in
`mmo_server_persistence.cpp` and the new split files.

Because persistence is known temporary and will be redesigned, the build target
currently compiles only `mmo_server_persistence.cpp` for those operations. The
split files remain as staged work but are not linked until the duplicate
extraction is completed.

## Verified

Built successfully:

```text
cmake --build build --target Gothic2Notr --config Debug
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Move from observation to authority:

1. Build a server-side MDS/model-script animation timing importer.
2. Key profiles by animation name, weapon state and body state.
3. Let accepted attack start create a predictive server timeline.
4. Reject/rollback melee damage outside the predicted hit window once distance,
   facing and `DamageCalculator` parity are also in place.
