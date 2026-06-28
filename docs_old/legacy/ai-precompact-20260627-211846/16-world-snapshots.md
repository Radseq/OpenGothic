# World Snapshots

Files: `game/game/worldstatestorage.h`, `game/game/worldstatestorage.cpp`.

`WorldStateStorage` serializes one `World` into an in-memory native snapshot and stores it as `worlds/<name>.zip` in a normal save.

It is useful for:

- validating DB restore against native save behavior;
- preserving visited worlds during world transitions;
- reverse-engineering coverage gaps.

It is not an MMO persistence model. Keep it as a compatibility oracle while structured baseline, deltas, and canonical DB tables become authoritative.

