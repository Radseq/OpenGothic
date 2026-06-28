# Combat State

Files: `game/world/objects/npc.cpp`, `game/game/fightalgo.h`.

- `Npc::tick` advances animation, AI, casting, regeneration, movement, and combat.
- `onNoHealth` is the death/unconscious transition: weapon cleanup, AI clear, state change, perception changes, and interaction release.
- `FightAlgo` chooses local combat instructions and attack distance; it is transient combat control state.
- Health, mana, death, consumable/ammunition counts, and durable quest/script effects are persistence candidates.

Do not persist live animation frame, fight queue, or local target-selection heuristics as MMO canonical state.

