# Repository Map — Full Client MMO Boundary

Last verified: 2026-07-12.

## Build integration

- `src/client/CMakeLists.txt` — links the sandbox facade and keeps SQLite tooling
  optional/off by default.

## Engine/sandbox bridge

- `src/client/game/game/mmoclientadapter.h` — engine-facing domain request API;
- `src/client/game/game/mmoclientadapter.cpp` — submission adapter;
- `src/client/game/game/mmoclientadapterdetail.h` — private fail-closed mapping
  used by implementation and focused tests;
- `src/client/game/game/mmoclientbridge.h`
- `src/client/game/game/mmoclientbridge.cpp`
- public facade: `src/client_sandbox/include/gothic/mmo/client_runtime_facade.h`

## Input and observations

- `src/client/game/game/mmosemantichooks.h`
- `src/client/game/game/mmosemantichooks.cpp`
- `src/shared/net/mmo/mmo_client_intent.h`
- `src/shared/net/mmo/mmo_client_intent_validation.h`

## Server presentation

- `mmoserverentitypresentationtypes.h` — protocol-independent handles, route
  scope, entity kinds and transform observations;
- `mmoserverentitypresentationregistry.*` — bounded centralized local binding;
- `mmoserverentityinterpolator.*` — route-scoped remote entity smoothing;
- `mmomovementcorrectionboundary.*` — validated local-player correction queue;
- `mmoserverdialogpresentation.h`
- `world/objects/npc.*` for the `mmoServerReplica` guard.

## Mode and UX

- `src/client/game/commandline.*`
- `src/client/game/ui/gamemenu.cpp`
- save/load/session integration in `gamesession.*`, `serialize.*` and menu code.

## Optional local tooling

- `src/client/tools/mmo` — SQLite capture/restore diagnostics, excluded from the
  production client unless explicitly enabled.

Do not recreate the removed client-side server tree.
