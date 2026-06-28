# Persistence CLI

`game/commandline.cpp` defines the runtime SQLite flags.

- `-mmo-sqlite <path>`: enable the backend and choose its DB.
- `-mmo-sqlite-interval-ms <n>`: delta-flush lower bound; the code clamps it to 250 ms.
- `-mmo-sqlite-no-restore`: capture only; do not apply DB state on session start.
- `-mmo-sqlite-capture-baseline`: capture immutable baseline only for the first session of a fresh DB started from New Game.

For normal testing use the same DB without the baseline flag. Never recapture a baseline from an existing save.

