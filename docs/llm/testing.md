# Full Client Validation

Use the narrowest sufficient layer, then run every higher layer affected by the
change. A passing lower layer must not be described as proof of graphical
integration.

## Static boundaries

```bash
python3 tools/check_client_mmo_sandbox_boundary.py --strict
python3 tools/check_llm_context.py --strict
```

The first check rejects full-client ownership of sockets/codecs/private sandbox
modules, filesystem gameplay side channels, production SQLite coupling and
legacy packet/result surfaces. The second checks canonical context and links.

## Focused sandbox plus presentation target

```bash
cmake -S src/client_sandbox -B build/mmo_client_sandbox -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DGOTHIC_MMO_CLIENT_SANDBOX_BUILD_TESTS=ON
cmake --build build/mmo_client_sandbox \
  --target gothic_mmo_client_transport_tests -j"$(nproc)"
ctest --test-dir build/mmo_client_sandbox --output-on-failure
```

This proves transport/facade behavior and selected protocol-independent client
components: adapter validation, mailbox ordering, bootstrap/state invariants,
identity-safe registries, interpolation/correction, catalog admission,
inventory/equipment read models and combat presentation.

It does **not** compile or execute the complete graphical executable,
`mmoclientbridge`, menu orchestration or all `GameSession` materializers.

## Normal client build

Configure/build the regular client with `src/client_sandbox` present. This
proves CMake composition and compilation of the complete integration, including
strict warnings. It does not prove runtime assets, rendering or multi-client
behavior. Also verify a standalone/native configuration when build logic or
mode separation changes.

## Process gate

Use the canonical Protocol V2 process-gate runner from the root documentation
for session, reconnect, bootstrap/resync and multi-client network behavior. It
proves real processes and transport but only the presentation surfaces observed
by its clients.

## Graphical smoke

Use `tools/run_mmo_graphical_client.py` with complete game assets. Verify at
minimum:

- native startup remains unchanged;
- server-bound menu/session reaches in-world or reports a clear failure;
- local/remote/NPC identities materialize and reroute/reset safely;
- movement prediction yields to server correction;
- interactions and combat submit exact typed intents without local damage;
- the changed UI reads authoritative revisions and handles rejection/resync;
- reconnect/resume does not retain objects or pending UI from the old route.

For renderer/materializer work, run two graphical clients. Record concise
results and limitations, not volatile counts or full logs.
