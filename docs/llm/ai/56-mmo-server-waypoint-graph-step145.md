# MMO server waypoint graph foundation - step 145

## What changed

- Added `server/cpp/mmo_server_waypoint_graph.h/.cpp`.
- The new waypoint graph is independent from MySQL and transport code.
- It provides:
  - waypoint nodes with key/name/kind/position;
  - directed and undirected edges;
  - O(1) key lookup through a heterogeneous `std::string_view`-friendly map;
  - directed edge and adjacency checks;
  - nearest waypoint lookup within a max distance;
  - path-step validation for `current -> next` plus target existence checks.

## Durable decision

Server-side NPC navigation should validate against a domain graph, not SQL strings or UDP payload glue.

The graph module is intentionally small and in-memory. A later repository can load it from the current bridge DB or from the future MMO database without changing NPC navigation authority logic.

## Verification

- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
- Result: success.

## Next good steps

- Add a loader/repository interface that can populate `Waypoint::Graph` from `mmo_server_waypoint_read_model` and `mmo_server_waypoint_edge_read_model`.
- Use `Graph::validatePathStep(...)` in waypoint authority once the graph is available per world instance.
- Consider caching one immutable graph snapshot per world template/instance.
