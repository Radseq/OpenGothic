# World Facade

Files: `game/world/world.h`, `game/world/world.cpp`.

`World` is the gameplay facade over `WorldObjects`, waypoint graph, physics, triggers, and rendering.

- `load` / `save` delegate persistent object state to `WorldObjects`.
- `npcById`, `itmById`, and `mobsiById` expose capture iteration; IDs are container indices, not durable identity.
- Use NPC/item persistent IDs and interactive VOB IDs for DB identity.
- `forEachWayPoint` and `forEachWayEdge` export navigation content.
- `addNpc`, `addItem`, `takeItem`, `removeItem`, and `removeNpc` are future semantic event hook sites.

World tick state includes AI and physics. Persist only explicitly classified durable components.

