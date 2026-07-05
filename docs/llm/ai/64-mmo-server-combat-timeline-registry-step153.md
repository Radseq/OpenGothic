# MMO server combat timeline registry - step 153

## What changed

Added the first server-side combat timeline module:

- `server/cpp/mmo_server_combat_timeline_authority.h`
- `server/cpp/mmo_server_combat_timeline_authority.cpp`

It is built by `server/cpp/CMakeLists.txt` and owned by the UDP server as:

```cpp
Mmo::Server::CombatTimeline::Registry gCombatTimelineRegistry;
```

## Why

Gothic melee combat is animation-timed. A hit is not valid merely because an NPC
is in combat. In the client, damage is committed when an attack animation reaches
an `OPTIMAL_FRAME`, and blocking depends on defensive animation windows.

The server therefore needs a combat timeline before it can become authoritative
for:

- attack start,
- prehit/windup,
- hit frame candidate,
- recovery,
- defence/parade,
- down/dead,
- combo continuation,
- damage correlation.

## Current model

`CombatTimeline::Registry` stores one observed combat snapshot per actor.

Phases:

- `Idle`
- `Ready`
- `Windup`
- `HitFrameCandidate`
- `Recovery`
- `Defence`
- `Down`
- `Dead`

The phase is derived from client-observed fields:

- `fight_state`
- `attack_state`
- `body_state`
- `attack_anim`
- `prehit`
- `down`
- `dead`
- `unconscious`

This follows the client audit:

- `prehit` means attack animation before future `OPTIMAL_FRAME`,
- `attack_anim`/`BS_HIT` means active attack animation,
- `BS_PARADE` means defensive/parade body state,
- `unconscious/down/dead` are terminal combat states.

## Integration

`RecordNpcFightState` now updates `gCombatTimelineRegistry` before persistence.

`ApplyWorldEntityDamage` now asks the registry for damage evidence:

```cpp
gCombatTimelineRegistry.evaluateDamageEvidence(...)
```

If an attacker timeline exists but the damage is outside a recent observed attack
window, the server logs:

```text
[combat_timeline_damage_mismatch]
```

It does not reject the damage yet.

## Why no hard rejection yet

The server still does not import MDS animation timings, Fight.dat move tables or
full `DamageCalculator` semantics. Rejecting damage now would risk diverging from
Gothic.

This module is an evidence and synchronization layer. It prepares the server for
authority without pretending to already be the final combat engine.

## Client payload support

Previous step added these fields to `RecordNpcFightState` payload:

- `body_state`
- `attack_anim`
- `prehit`
- `weapon_state_id`

This step consumes those fields on the server.

## Next good step

Create a server-side `combat_animation_profile` importer/model that can represent
the timing data currently coming from MDS animations:

- animation key,
- total time,
- `OPTIMAL_FRAME`,
- `HIT_END`,
- `PARRY_FRAME`,
- `COMBO_WINDOW`,
- body state,
- weapon state.

After that, the timeline registry can become predictive instead of only
observational: the server will accept an attack animation at start tick and know
when the valid hit/parry/combo windows occur.
