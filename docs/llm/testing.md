# Testing — Full Client MMO Boundary

## Static boundary

```bash
python3 tools/check_client_mmo_sandbox_boundary.py --strict
python3 tools/check_llm_context.py --strict
rg -n "Net::Client|Packet[[:space:]]+packet|submitClientIntent" \
  src/client/game/game/mmosemantichooks.cpp src/client/game/ui/dialogmenu.cpp
```

The final `rg` command must return no matches.

## Focused adapter/presentation tests

```bash
cmake -S src/client_sandbox -B build/mmo_client_sandbox -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DGOTHIC_MMO_CLIENT_SANDBOX_BUILD_TESTS=ON
cmake --build build/mmo_client_sandbox \
  --target gothic_mmo_client_transport_tests -j"$(nproc)"
ctest --test-dir build/mmo_client_sandbox --output-on-failure
```

Coverage includes:

- domain-to-Protocol-V2 mapping and fail-closed validation of incomplete legacy
  engine DTOs;
- route/entity-generation checks and exact despawn;
- forced rebind and local-object alias prevention;
- bounded interpolation, stale/route rejection and exact erase;
- local-player movement-correction route/identity/tick checks;
- atomic typed bootstrap activation and invalid-bootstrap rollback;
- stale route epoch/world-generation rejection;
- local/remote/NPC roster classification, exact generation replacement and
  despawn;
- revisioned transform/NPC/dialog/interactive/mover application and
  reconciliation versus hard-snap correction classification;
- fake-facade mailbox mapping, deterministic cross-mailbox `streamSequence`
  ordering, bootstrap conversion and fail-closed malformed-record rejection.

## Full client

Configure the normal client build with the workspace sandbox available. Verify:

- native single-player startup without MMO flags;
- MMO facade startup/shutdown;
- in-memory bootstrap status and snapshot delivery;
- route replacement clears old presentation bindings;
- local/remote-player/NPC classification;
- server-replica identity/interpolation and safe despawn;
- presentation-catalog decode, manifest admission and exact NPC resource
  lookup, including fail-closed replacement of a previously installed catalog;
- dialog choice submission and presentation;
- clean handling of missing ASIO backend/facade.

Record commands actually run. Do not preserve local credentials, absolute paths
or full logs in canonical context.

## Graphical client smoke run

Build the server and the normal graphical client in a complete checkout, then:

```bash
python3 tools/run_mmo_graphical_client.py \
  --server-exe build/mmo_cpp_server/mmo_udp_server \
  --client-exe build/client/opengothic/Gothic2Notr \
  --gothic-dir "/path/to/Gothic II"
```

The default run starts the production UDP server with the deterministic
Protocol V2 gameplay fixture and launches the client with `-nomenu`. Verify in
`runtime/graphical-mmo/server.log` and the client log:

- session reaches `in_world`;
- local player accepts keyboard movement while server corrections remain
  authoritative;
- replicated players/NPCs materialize;
- Talk/Loot/Use resolves an exact server handle;
- draw/holster/attack/parry submit typed combat actions;
- local attacks do not mutate replicated NPC hit points;
- stopping/restarting transport uses the resume ticket when the server durable
  state remains available.

Use `--menu` to verify typed roster-backed New Game and Continue/Load, or
`--existing-server` when the server is started separately.
