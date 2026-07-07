# MMO server perception reaction planner - step 180

## What changed

Added a small server-side reaction planning module:

- `server/cpp/mmo_server_perception_reaction_planner.h`
- `server/cpp/mmo_server_perception_reaction_planner.cpp`

The planner consumes a queued Gothic II perception event plus the current
witness summary and classifies the intended server reaction.

## Reaction kinds

Current plans are deliberately coarse:

- `ignore`
- `observe`
- `queue_script`
- `interrupt`
- `warn`
- `suspect_crime`
- `call_help`
- `start_combat`

Each plan also carries flags for:

- UDP replication needed,
- Gothic II script/perception handler needed,
- current action interruption,
- hostility/combat state change.

## UDP boundary

This is runtime-only state. It does not use the database or outbox as a
gameplay transport.

The log flag `udp_replication=1` marks events that should become server-to-client
live UDP events once the typed NPC/perception replication packet is added.

## Server integration

After `[perception_witnessed]`, the server now logs:

```text
[perception_reaction_planned] seq=<id> perc=<event> reaction=<kind> udp_replication=<0|1> script_handler=<0|1> interrupt=<0|1> hostility=<0|1> reason=<reason>
```

No script handler is invoked yet and no NPC AI state is mutated in this step.
That is intentional: exact guard, guild, faction and attitude behavior must be
checked against Gothic II scripts/client behavior before becoming authoritative.

## Next good step

Add a typed server-to-client UDP live event for NPC perception reactions, then
wire only the safe visible parts first:

- interruption/cancel of current NPC action,
- start combat/call help as visible intent,
- crime suspicion/warn as visible bark/action request.

After that, inspect Gothic II perception handlers and map the script outcomes
into this planner instead of inventing custom guard behavior.
