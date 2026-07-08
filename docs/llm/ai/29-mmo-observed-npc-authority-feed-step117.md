# MMO observed NPC authority feed - Step117

Step117 turns the previously empty NPC authority snapshot domains into live data in server-bound client mode.

## What changed

- `GameSession` samples nearby non-player NPCs around the hero every 2.5 seconds while `-mmo-client-server` is active.
- The sampler keeps a compact signature cache per NPC and emits only changed or stale records, capped to 8 NPCs per sweep.
- `Mmo::Hooks::onObservedNpcAuthorityState` emits four existing semantic action types:
  - `record_npc_routine_state`
  - `record_npc_ai_state`
  - `record_npc_path_state`
  - `record_npc_fight_state`
- Payloads use stable `npc:<world>:pid:<id>:sym:<symbol>` keys and stable `waypoint:<world>:<name>` keys.
- The UDP server logs raw action id and datagram size for `bad_action_kind`, which makes enum/protocol drift diagnosable.

## Expected runtime effect

After the first live snapshot refreshes, logs that previously showed:

```text
npc_routine_state=0 npc_ai_state=0 npc_path_state=0 npc_fight_state=0
```

should start showing positive counts when nearby NPCs are present and the client has had at least one sampling interval.

This is still not a full authoritative server AI tick. It is the next durable layer for that: the server database now receives routine, AI, pathing and fight observations for nearby NPCs without relying on native save files.




