# API Contracts — Full Client MMO Boundary

The engine-facing input boundary starts in `mmoclientadapter.h`. Domain systems
submit presentation/input DTOs that do not expose packet kinds, route headers,
sequence allocation, codecs, sockets or ASIO types.

The first implemented request is `ClientMovementIntent`. It contains two
movement samples, reversible client movement-state hints, cadence values and
bounded identity views. `submitClientMovement` validates and synchronously
copies the request into the current compatibility facade contract. Character
stats and authoritative gameplay outcomes are structurally absent.

`mmoclientbridge.h` remains the transitional integration boundary around
`ClientRuntimeFacade`. Its stable responsibilities are:

- configure/start/stop the facade from command-line MMO mode;
- submit validated compatibility intents while domain adapters are migrated;
- drain server live deltas, dialogs, entity transforms, bootstrap snapshots,
  bootstrap ACK/status, diagnostics and faults;
- translate server entity handles into local presentation identities.

Wire schema, endpoint retry and packet sequencing are not client API concerns.
New engine features must consume domain requests/presentation events rather than
including shared wire headers directly where practical.
