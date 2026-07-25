# Full Client Validation

## Purpose

Define validation layers for full-client MMO changes and the evidence each
layer provides. A passing lower layer must not be described as graphical or
production runtime proof.

## API and contracts

### Static boundaries

```bash
python3 tools/check_client_mmo_sandbox_boundary.py --strict
python3 tools/check_llm_context.py --strict
```

The boundary check rejects full-client ownership of sockets, codecs, private
sandbox modules, filesystem gameplay side channels, production SQLite coupling
and retired compatibility contracts. The context check validates canonical
documentation and links.

### Focused sandbox and presentation target

```bash
cmake -S src/client_sandbox -B build/mmo_client_sandbox -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DGOTHIC_MMO_CLIENT_SANDBOX_BUILD_TESTS=ON
cmake --build build/mmo_client_sandbox \
  --target gothic_mmo_client_transport_tests -j"$(nproc)"
ctest --test-dir build/mmo_client_sandbox --output-on-failure
```

This proves transport/facade behavior and the selected protocol-independent
client components compiled into that target. It does not instantiate the full
OpenGothic executable, menu orchestration or every `GameSession` materializer.

### Normal client build

Configure and build the regular client with `src/client_sandbox` present. This
proves production CMake composition, strict-warning compilation and linkage of
the complete client source set. Also verify a standalone/native configuration
when mode or build separation changes.

### Process and graphical gates

Use the canonical Protocol V2 process-gate runner for real-process session,
reconnect, bootstrap/resync and multi-client transport behavior. Use
`tools/run_mmo_graphical_client.py` with complete game assets for engine,
renderer, audio and UI proof.

## Data and state

Validation evidence is classified as:

| Layer | Proves | Does not prove |
|---|---|---|
| Static checker | forbidden dependency and documentation rules | compilation or runtime behavior |
| Focused test target | selected facade/presentation invariants | complete graphical composition |
| Normal client build | complete source composition and linkage | assets, interaction or multi-client runtime |
| Process gate | real process/session/transport behavior | unobserved graphical surfaces |
| Graphical smoke | engine/UI/materializer behavior with assets | exhaustive authority or persistence correctness |

Record reproducible command names, failures and relevant environment
prerequisites. Do not copy volatile logs or test counts into canonical state.

## Dependencies

- Focused tests are declared by `src/client_sandbox/CMakeLists.txt` and include
  selected sources from `src/client/game/game`.
- Full executable composition is declared by `src/client/CMakeLists.txt`.
- Server and protocol process gates are owned by root documentation and their
  dedicated runners.
- Private assets are required only for graphical execution, not for static or
  focused headless validation.

## Examples

A route-state header change requires static checks and the focused presentation
target. If it changes `GameSession` application, also build the normal client
and run graphical reroute/reconnect smoke.

A CMake mode-separation change requires both workspace/server-bound and
standalone/native configurations.

Graphical smoke should verify at minimum:

- native startup remains unchanged;
- server-bound startup reaches in-world or reports a clear MMO failure;
- local, remote and NPC identities materialize and reroute safely;
- prediction yields to authoritative correction;
- interactions/combat submit typed intent without local gameplay results;
- changed UI handles authoritative revision, rejection and resync;
- reconnect does not retain objects or pending UI from the old route.
