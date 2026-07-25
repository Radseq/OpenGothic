# Full Client Ownership Map

## Purpose

Map stable ownership seams for `src/client`. This is not a complete file
inventory; use the repository index for concrete symbols, direct callers and
focused tests.

## API and contracts

| Concern | Owner / stable anchor |
|---|---|
| Full executable composition and optional tooling | `src/client/CMakeLists.txt` |
| Public MMO transport/session boundary | `src/client_sandbox/include/gothic/mmo/client_runtime_facade.h` |
| Engine session and typed intent bridge | `src/client/game/game/mmoclientbridge.h` |
| Protocol-independent presentation records | `src/client/game/game/mmoserverpresentationevents.h` |
| Facade-to-presentation conversion | `src/client/game/game/mmoserverpresentationfacadeadapter.h` |
| Ordered batch application | `src/client/game/game/mmoserverpresentationbatchconsumer.h` |
| Route/bootstrap/live-event state | `src/client/game/game/mmoserverpresentationstate.h` |
| Inventory/equipment read models | `src/client/game/game/mmoserverinventoryreadmodel.h` |
| Engine projection | `GameSession` MMO implementation units under `src/client/game/game` |
| Menu and input activation | `MainWindow` and adjacent UI/game controllers |
| Focused transport/presentation tests | target declared by `src/client_sandbox/CMakeLists.txt` |
| Architectural boundary enforcement | `tools/check_client_mmo_sandbox_boundary.py` |

## Data and state

The stable state layers are:

1. sandbox runtime/session state;
2. transient facade mailbox snapshot;
3. accepted protocol-independent presentation state;
4. engine-object registries and visual/read-model projection;
5. transient UI selection and pending indicators.

Do not add a parallel filesystem, JSON, SQLite or packet-shaped state path to
the full client.

## Dependencies

```text
OpenGothic UI/input/engine
        -> full-client bridge and presentation projection
        -> public client_sandbox facade
        -> shared protocol contracts
        -> authoritative server
```

The client may include the installed public facade. It must not include
server-private gameplay headers or private sandbox transport/modules.

## Examples

For an inventory UI change, retrieve:

1. the UI/input caller;
2. the relevant bridge submission function;
3. `ServerInventoryPresentationState` symbols;
4. direct focused tests;
5. `src/client/CMakeLists.txt` and sandbox test-target ownership.

For a new presentation event, retrieve the facade DTO, adapter mapping,
`ServerPresentationState::applyOne` overload, `GameSession` materializer and
focused tests before editing.
