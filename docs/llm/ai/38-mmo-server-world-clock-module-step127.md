# Step127 - MMO server world clock module

Goal: start turning the monolithic C++ UDP server into server-authoritative
domain modules without changing protocol behavior.

Implemented:
- Added `server/cpp/mmo_server_world_clock.h`.
- Added `server/cpp/mmo_server_world_clock.cpp`.
- Registered the new source file in `server/cpp/CMakeLists.txt`.
- Moved world-clock bootstrap snapshot SQL construction out of
  `mmo_udp_server.cpp` into `Mmo::Server::buildWorldClockSnapshotQuery`.
- Added small C++23-friendly world-clock primitives:
  `WorldClockMinuteMs`, `WorldClockHourMs`, `WorldClockDayMs`,
  `WorldClockTime` and `advanceWorldClock`.

Current behavior:
- No protocol change.
- No DB schema change.
- No runtime authority change yet.
- Bootstrap snapshots still expose the same `world_clock` JSON object.

Why this matters:
- `world_clock` is the first server gameplay domain with its own module.
- Future work can attach authoritative tick, sleep/rest effects, delayed timers
  and routine scheduling here instead of growing `mmo_udp_server.cpp`.
- This follows the production direction: small modules, clear ownership and
  replaceable domain services.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
- `RelWithDebInfo` compilation reached link, but the final executable was locked
  by a running `mmo_udp_server.exe` process.
