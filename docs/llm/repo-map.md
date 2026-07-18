# Full Client Ownership Map

This is a map of stable seams, not a file inventory. Retrieve concrete symbols,
callers and tests through the repository index.

| Concern | Owner / stable anchor |
|---|---|
| Full executable composition and optional tooling | `src/client/CMakeLists.txt` |
| Public MMO transport/session boundary | `src/client_sandbox/include/gothic/mmo/client_runtime_facade.h` |
| Engine-facing session and intent bridge | `src/client/game/game/mmoclientbridge.h` |
| Protocol-independent presentation records | `src/client/game/game/mmoserverpresentationevents.h` |
| Route/bootstrap/live-event state machine | `src/client/game/game/mmoserverpresentationstate.h` |
| Facade-to-presentation conversion | `src/client/game/game/mmoserverpresentationfacadeadapter.h` |
| Engine projection | `GameSession` MMO implementation units under `src/client/game/game` |
| Menu and input activation | `MainWindow` and adjacent UI/game controllers |
| Focused transport/presentation tests | target declared by `src/client_sandbox/CMakeLists.txt` |
| Architectural boundary enforcement | `tools/check_client_mmo_sandbox_boundary.py` |

## Dependency direction

```text
OpenGothic UI/input/engine
        -> full-client bridge and presentation projection
        -> public client_sandbox facade
        -> shared protocol contracts
        -> authoritative server
```

Reverse ownership is forbidden. In particular, the client may include the
public facade but must not include server-private gameplay headers or private
sandbox transport/modules.

For a change, retrieve the edited symbol, all direct callers, its tests and the
CMake owner. Do not enlarge this document with results that the index can
reconstruct.
