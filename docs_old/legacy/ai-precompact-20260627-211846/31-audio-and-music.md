# Audio And Music

Files: `game/sound/soundfx.*`, `game/world/worldsound.*`, `game/gamemusic.*`, `game/world/objects/sound.*`.

- `SoundFx` resolves script sound definitions and randomized variants into playable buffers.
- `WorldSound` owns positional effects, ambient zones, dialog playback, occlusion, and listener-range updates.
- `GameMusic` owns the active theme and day/night, standard, threat, or fight tags.
- Audio effect handles and active playback are process-local presentation state.

Persist only gameplay facts that affect a later audible result, such as a durable world object state or quest phase. Recreate sounds from restored world state; do not restore device handles or playback cursors.

