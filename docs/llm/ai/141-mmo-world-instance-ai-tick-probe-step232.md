# Step232 - Manual World Instance AI Tick Probe

Purpose: add a reusable manual `world_instance` AI tick boundary before any
automatic scheduler exists.

Changed:

- `server/cpp/mmo_world_instance_ai_tick.*`
- `server/cpp/mmo_world_instance_ai_tick_probe.cpp`

Flow:

```text
WorldInstanceContentCache
-> Step231 guarded runtime actors
-> Step229 NPC perception policy
-> Step230 decision recorder only when --write
```

Contract:

- Probe is dry-run by default.
- DB mutation requires `--write` and bounded `--max-records`.
- Output reports NPC identity counters, actor counts, decision counts and
  record-limit skips.
- Idempotency uses server tick plus NPC/target/perception identity.

No UDP scheduling, packet fan-out, live dialog, movement, combat or Daedalus VM
execution is enabled.
