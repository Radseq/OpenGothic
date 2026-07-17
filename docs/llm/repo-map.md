# Repository Map — Full Client MMO Boundary

Last verified: 2026-07-17.

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
- `src/client/game/game/mmosemantichooks.cpp` — public forwarding surface;
- `mmosemantichooks_internal.h` and focused `mmosemantichooks_*.cpp` units for
  identity, encoding, submission, character, inventory, combat, dialog/story,
  NPC AI, world and interactive/mover behavior.
- `src/shared/net/mmo/mmo_client_intent.h`
- `src/shared/net/mmo/mmo_client_intent_validation.h`

## Server presentation

- `mmoserverpresentationevents.h` — protocol-independent typed route,
  bootstrap and live presentation DTOs;
- `mmoserverpresentationstate.h` — bounded route/baseline/revision state;
- `mmoserverpresentationbatchconsumer.h` — authority-ordered fake-mailbox and
  production mailbox cut application (`route -> bootstrap -> live`);
- `mmoserverpresentationfacadeadapter.h` — facade DTO to full-client DTO map;
- `mmoserverentitypresentationtypes.h` — exact local binding observations;
- `mmoserverentitypresentationregistry.*` — bounded centralized local binding;
- `mmoserverentityinterpolator.*` — route-scoped remote entity smoothing;
- `mmomovementcorrectionboundary.*` — validated local-player correction queue;
- `gamesession.*` — engine sink for materialization, correction and presenters;
- `ui/dialogmenu.*` — typed dialog lifecycle UI;
- `world/objects/npc.*` — server-replica AI and lifecycle presentation guards.

## Large engine implementation splits

- `gamescript.cpp` plus `gamescript_bindings.cpp` and
  `gamescript_external_*.cpp` own VM binding and external families;
- `gamesession.cpp` plus `gamesession_dialog.cpp`, `gamesession_startup.cpp`,
  `gamesession_tick.cpp`, `gamesession_persistence.cpp`,
  `gamesession_world_lifecycle.cpp` and `gamesession_mmo_*.cpp` own focused
  session lifecycle and MMO presentation/restore responsibilities.

## Mode and UX

- `src/client/game/commandline.*`
- `src/client/game/ui/gamemenu.cpp`
- save/load/session integration in `gamesession.*`, `serialize.*` and menu code.

## Optional local tooling

- `src/client/tools/mmo` — SQLite capture/restore diagnostics, excluded from the
  production client unless explicitly enabled.

Do not recreate the removed client-side server tree.
