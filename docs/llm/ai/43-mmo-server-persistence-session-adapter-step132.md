# Step132 - MMO server persistence session adapter

Goal: move the first full DB operation group out of `mmo_udp_server.cpp` and
behind the persistence bridge.

Implemented:
- `mmo_server_persistence` now owns:
  - `dbLogin`;
  - `isActiveDbSession`;
  - `ensureActiveDbSession`;
  - the temporary bridge helper that auto-creates a missing DB character from
    the `PC_HERO` template.
- `mmo_udp_server.cpp` now asks persistence for login/session recovery instead
  of building those SQL statements locally.

Current behavior:
- No protocol change.
- No DB schema change.
- The same bridge behavior remains: missing selected characters can be created
  from the `PC_HERO` template for local MMO dev flow.

Why this matters:
- Session lifecycle is now a persistence adapter responsibility.
- This is the first extraction of a whole DB operation group, not just generic
  SQL/string helpers.
- Future DB redesign should replace this implementation behind the same server
  boundary rather than spreading new session SQL through packet handling.

Verification:
- `cmake --build build\mmo_cpp_server --target mmo_udp_server --config Debug`
  succeeds.
