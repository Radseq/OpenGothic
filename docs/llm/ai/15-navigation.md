# Navigation Content

Files: `game/world/waypoint.h`, `game/world/waymatrix.h`, `game/world/waymatrix.cpp`.

`WayMatrix` owns waypoints, free points, start points, edges, and path search.

- Use `World::forEachWayPoint` and `World::forEachWayEdge` for DB export.
- Waypoint position, direction, type, connections, and ladder links are content/navigation data.
- `WayPoint::useCounter` and temporary path fields are runtime state, not durable MMO state.
- NPC current/routine/move-target waypoints are checkpoints; active path queues remain transient.

Do not rebuild the navigation graph on periodic persistence flushes unless the content revision changed.

