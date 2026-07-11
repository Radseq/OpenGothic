# Testing — Full Client MMO Boundary

## Static boundary

```bash
python3 tools/check_client_mmo_sandbox_boundary.py --strict
python3 tools/check_llm_context.py --strict
```

## Sandbox first

Build and run client-sandbox tests before engine integration. The headless path
must prove protocol/session behavior without rendering or private assets.

## Full client

Configure the normal client build with the workspace sandbox available. Verify:

- native single-player startup without MMO flags;
- MMO facade startup/shutdown;
- in-memory bootstrap status and snapshot delivery;
- server-replica identity/interpolation;
- dialog choice submission and presentation;
- clean handling of missing ASIO backend/facade.

Record commands actually run. Do not preserve local credentials, absolute paths
or full logs in canonical context.
