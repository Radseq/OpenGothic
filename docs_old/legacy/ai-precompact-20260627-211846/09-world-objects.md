# World Objects Storage

Files: `game/world/worldobjects.h`, `game/world/worldobjects.cpp`.

`WorldObjects` owns NPC arrays, world item arrays, interactives, trigger queues, routines, and persistent-id allocation.

- `load` / `save` serialize NPCs, invalid NPCs, world items, VOB trees, trigger events, and interactive routines.
- `npcArr` is sorted during tick; never use its index as NPC identity.
- `nextNpcPersistentId` and `nextItemPersistentId` allocate durable source IDs.
- `npcRemoved` intentionally retains objects with possible dangling gameplay references; do not treat its lifetime as a despawn event without an explicit hook.
- `addItem*`, `takeItem`, `removeItem`, `addNpc`, and `removeNpc` are preferred future event-source boundaries.

