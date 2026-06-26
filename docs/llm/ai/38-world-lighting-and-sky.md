# World Lighting And Sky

Files: `game/world/worldlight.*`, `game/graphics/sky/*`, `game/graphics/lightgroup.*`, `game/graphics/worldview.*`.

- `WorldLight` bridges a static Gothic light VOB to a `LightGroup` render handle and updates it when the VOB moves.
- `Sky` derives sun, moon, clouds, ambient light, and day/night state from the world clock.
- `WorldView` submits lighting and sky state each frame through `SceneGlobals`.

The world clock and durable light-VOB changes are persistence candidates. Derived colors, cloud offsets, shadow maps, light handles, and render buffers must be recalculated locally after load.
