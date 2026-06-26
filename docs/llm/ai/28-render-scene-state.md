# Render Scene State

Files: `game/graphics/worldview.*`, `game/graphics/sceneglobals.*`, `game/graphics/visualobjects.*`, `game/graphics/rtscene.*`.

- `WorldView` mirrors gameplay world visuals and performs per-frame visibility and draw preparation.
- `SceneGlobals` packages camera, sky, light, depth, shadow, and frame data for GPU bindings.
- `VisualObjects` owns render-object handles, instance storage, draw buckets, clusters, and draw commands.
- `RtScene` builds optional acceleration structures from static scene material and mesh data.

Render handles and GPU instance IDs are invalid across process restarts. Persist the gameplay VOB/NPC/item transform and visual definition, then recreate the view during world load.

