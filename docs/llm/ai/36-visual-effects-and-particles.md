# Visual Effects And Particles

Files: `game/graphics/visualfx.*`, `game/graphics/pfx/*`, `game/world/objects/pfxemitter.*`, `game/game/definitions/visualfxdefinitions.*`.

- `VisualFx` parses Daedalus visual-effect definitions, trajectories, collisions, keys, sound, lights, and lifetime.
- `ParticleFx` describes particle emission and material behavior; `PfxObjects` owns live particle instances.
- `PfxEmitter` is the world-object bridge for persistent map emitters and dynamic visual instances.
- Spell code can create effects, but gameplay outcome must not depend only on a visible particle completing.

Particle buffers, emit counters, trails, and effect handles are transient. Persist a durable source state only when the gameplay design requires it, then recreate the visual effect after restore.

