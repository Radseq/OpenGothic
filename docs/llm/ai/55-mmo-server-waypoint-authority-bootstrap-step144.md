# MMO server waypoint authority and bootstrap split - step 144

## What changed

- Added `server/cpp/mmo_server_waypoint_authority.h/.cpp`.
- NPC routine and path observations now pass through waypoint authority before persistence:
  - `buildNpcRoutineCommand(...)`;
  - `buildNpcPathCommand(...)`;
  - `canonicalWaypointRef(...)`.
- Waypoint authority canonicalizes waypoint inputs from:
  - `*_waypoint_key`;
  - `*_waypoint_name`;
  - legacy `*_waypoint`.
- Authority keys like `waypoint:<world>:<kind>:<index>:<name>` are reduced to the Gothic waypoint name for DB-facing routine/path state.
- `mmo_udp_server_direct_world_state_apply.inl` is now orchestration: parse payload fields, call waypoint authority, persist accepted commands.
- `mmo_udp_server_payload_mapper.inl` now forwards richer waypoint fields for NPC routine/path actions.

## Bootstrap split

- Added `server/cpp/mmo_server_waypoint_bootstrap.h/.cpp`.
- Nearby waypoint bootstrap SQL moved out of `mmo_udp_server_bootstrap_snapshot.inl`.
- `mmo_udp_server_bootstrap_snapshot.inl` now calls `Mmo::Server::Waypoint::readNearbyWaypointBootstrap(...)`.

## Durable decision

Waypoint normalization and NPC navigation command building belong in waypoint authority, not in transport or SQL code.

The current MySQL-backed waypoint bootstrap remains a temporary bridge. Keeping it in a focused module makes it easier to replace later with a real MMO world/navigation repository.

## Verification

- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
- Result: success.

## Next good steps

- Add a waypoint graph/query interface independent of MySQL.
- Add server-side route/path validation once waypoint edges are available through a real repository.
- Move nearby NPC bootstrap SQL out in the same style as nearby waypoint bootstrap.
