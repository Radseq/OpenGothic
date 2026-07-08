# Step231 - Runtime NPC Identity Guard

Purpose: prevent live AI recording from accepting runtime NPC rows with weak
identity such as `creature:None`, empty `npc_instance`, `:null`, `:undefined`
or `pid:-1`.

Changed:

- `server/cpp/mmo_npc_perception_runtime_source.*`
- `server/cpp/mmo_npc_perception_record_probe.cpp`

Contract:

- `RuntimeActorQueryOptions` defaults to repairing weak NPC entity keys when a
  stable content template/instance and `world_entity_state_id` are available.
- Rows without a content-backed template/instance are skipped by default.
- `--include-weak-npc-identity` is diagnostic only.
- Probe output includes `npc_identity` counters for rows read, accepted,
  repaired and skipped.

This step still does not run automatic AI, Daedalus VM, NPC movement or dialog
fan-out. It only guards the actor-source used by later manual AI probes.
