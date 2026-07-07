# MMO server fight move model - step 158

## What changed

Added a first server-side model for Gothic fight move selection and expansion:

- `server/cpp/mmo_server_fight_move_model.h`
- `server/cpp/mmo_server_fight_move_model.cpp`

The model mirrors the stable parts of client `FightAlgo`:

- table selection order from `FightAlgo::fillQueue`,
- script move expansion from `FightAlgo::nextFromQueue`,
- basic execution legality for attack/block/jumpback cases.

## Current modeled table selection

The server can classify the current combat context into choices equivalent to
the client-side Fight.dat buckets:

- `enemy_prehit`
- `enemy_stormprehit`
- `my_w_strafe`
- `my_w_runto`
- `my_w_focus`
- `my_w_nofocus`
- `my_g_runto`
- `my_g_focus`
- `my_fk_focus_mag`
- `my_fk_nofocus_mag`
- `my_fk_focus_far`
- `my_fk_nofocus_far`
- `fallback_my_w_nofocus`

This follows the audited order in `FightAlgo::fillQueue`.

## Current modeled move expansion

The server can expand script moves into execution actions:

- `TURN` -> `turn`
- `RUN` -> `move`
- `JUMP_BACK` -> `jump_back`
- `STRAFE` -> `strafe_left/right`, `strafe_end`
- `ATTACK` -> `attack`
- `ATTACK_SIDE` -> `attack_left`, `attack_right`
- `ATTACK_FRONT` -> randomized side attack, `attack`
- `ATTACK_TRIPLE` -> three-hit sequence
- `ATTACK_WHIRL` -> four side attacks
- `ATTACK_MASTER` -> six-hit sequence
- `TURN_TO_HIT` -> `turn_to_hit`
- `PARRY` -> `block`
- `WAIT`/`WAIT_EXT` -> `wait`
- `WAIT_LONGER` -> `wait_long`

Random choices are represented as an injected boolean for now. The final server
must replace that with deterministic server RNG suitable for tick replay.

## Timeline integration

`mmo_server_combat_timeline_authority.*` now derives and stores a diagnostic
`fightTableChoice` in each combat snapshot.

The client observation bridge now also sends:

- `actor_running`
- `opponent_running`
- `opponent_prehit`
- `opponent_attack_range`

These fields let the server classify branches such as "enemy prehit" and
"enemy storm prehit" using the same context checked by the client.

## Boundary

This is not full server AI yet.

The missing high-value pieces are:

- importing Fight.dat tables server-side,
- importing `fight_range_g` and ranged/magic range data,
- deterministic server RNG,
- server-owned action queue per NPC,
- authoritative transform snapshots for exact action ticks,
- hard ACK/NACK for illegal player/NPC combat intents.

For now this module is a production-shaped semantic model and diagnostic bridge,
not the final NPC brain.

## Verified

Built successfully:

```text
cmake --build build --target Gothic2Notr --config Debug
cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug
```

## Next good step

Create a `fight_intent_authority` layer:

- accept proposed attack/block/move combat intents,
- evaluate them through `fight_move_model`, spatial validation and animation
  windows,
- produce soft NACK/logging first,
- later become hard server authority once DamageCalculator and content importers
  are ready.
