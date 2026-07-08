# Step234 - UDP Server AI Startup Evidence Hook

Purpose: wire Step233 evidence into `mmo_udp_server` as an explicit startup-only
dry-run hook.

Changed:

- `server/cpp/mmo_server_types.h`
- `server/cpp/mmo_udp_server.cpp`
- `server/cpp/mmo_world_instance_ai_tick_probe.cpp`

Contract:

- `--world-instance-ai-startup-dry-run` runs one dry-run evidence tick.
- Strict/fail flags can fail startup with exit code `3` on rejected evidence.
- The hook requires MySQL plus runtime read-model/cache flags.
- It forces `dryRun=true`, `maxRecords=0`, `enqueueAction=false`.
- Logs must show `write_executed=0`.
- `--startup-check-only` reports `world_instance_ai_startup_dry_run=off`,
  `accepted` or `evidence_failed`.

No periodic scheduler, DB AI write, Daedalus VM, dialog fan-out, movement or
combat execution is enabled.
