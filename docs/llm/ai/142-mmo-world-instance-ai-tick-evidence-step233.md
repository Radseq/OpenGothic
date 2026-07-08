# Step233 - World Instance AI Tick Evidence Guard

Purpose: evaluate a completed manual AI tick before any future scheduler can
trust it.

Changed:

- `server/cpp/mmo_world_instance_ai_tick_evidence.*`
- `server/cpp/mmo_world_instance_ai_scheduler_boundary.*`
- `server/cpp/mmo_world_instance_ai_tick_probe.cpp`

Contract:

- Evidence checks accepted NPCs, player actors, produced decisions, weak NPC
  identity skips, missing `npc_instance` skips and record-limit skips.
- Strict probe flags can require actor pairs, decisions and clean NPC identity.
- Evidence failure returns `status=evidence_failed` and exit code `3`.
- Scheduler boundary is disabled by default and must run evidence before any
  future write tick.

No automatic `mmo_udp_server` scheduler is enabled in this step.
