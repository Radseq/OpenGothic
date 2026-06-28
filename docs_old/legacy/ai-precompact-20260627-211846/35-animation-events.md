# Animation Events

Files: `game/graphics/mesh/animation.*`, `game/graphics/mesh/animationsolver.*`, `game/graphics/mdlvisual.*`, `game/world/objects/npc.cpp`.

- `Animation::Sequence` holds model-script timing, movement translation, combo/parry windows, sound events, particle events, and tags.
- `processEvents`, `processSfx`, and `processPfx` consume animation time ranges.
- `MdlVisual` and the animation solver own blend, pose, and current sequence presentation state.
- NPC gameplay selects sequences, but a live frame/pose is not a durable identity or save model.

For MMO combat, use explicit server timestamps and action phases. Animation windows can define validation content, but do not persist GPU pose state or replay a client frame as authority.

