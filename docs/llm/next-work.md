# Next Work

## Immediate Step

Step231: fix runtime NPC actor identity before enabling automatic live
perception recording.

Step230 can already:

- read active players from `server_sessions` + `character_positions`;
- read active NPC/creature rows from `world_entity_state`;
- run Step229 `mmo_npc_perception_policy`;
- record explicit decisions through
  `mmo_ai_record_npc_perception_decision(...)`.

The current live DB sample still produced an NPC with `entity_key=creature:None`
and empty `npc_instance`. Do not schedule that as production AI truth.

Target inputs:

- runtime NPC rows with stable `entity_key`;
- content-backed NPC template key or Daedalus `npc_instance`;
- active player session/character UUIDs;
- Step230 writer;
- server tick from `realm_world_instances.current_tick` or a future tick source.

Expected result:

- Runtime actor-source skips or repairs weak NPC identity instead of recording
  ambiguous NPC decisions.
- A small explicit world_instance AI tick entrypoint can record valid decisions
  for active actors.
- No movement, dialog or combat broadcast yet.
- No per-player script VM or duplicated script truth is introduced.

## Acceptance Checks

- Step226/227/229/230 probes still build and pass their focused checks.
- `mmo_udp_server --startup-check-only` still loads the cache successfully.
- Live actor dry-run reports stable NPC identity before automatic recording is
  enabled.
- `mmo_ai_runtime` recording remains idempotent and goes through
  `mmo_ai_record_npc_perception_decision(...)`.

## Defer

Do not implement yet:

- full Daedalus VM execution for all scripts;
- per-player script VMs;
- live NPC path/movement replication;
- dialog UI/audio fan-out;
- trade/economy rewrite;
- final production DB rewrite.

These need stable read-model/cache/runtime actor identity and an explicit
world_instance tick boundary first.
