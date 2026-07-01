# 13 Step49 Server Bootstrap / NPC Navigation Questions

Step48 proved the Xardas fireplace chain is visible as player-caused interaction + trigger/mover evidence:
`use_interactive`, `update_interactive_state`, `trigger_event`, `mover_state_changed`.
The remaining question is no longer "can hooks see the world"; it is "what can the server safely own after restart".

## Decision

Do not make SQLite authoritative and do not run Gothic AI from MySQL directly.
For now, enable read-only probes that answer whether the current capture/projection has enough material to build a server runtime model.

Added tools:
- `tools/inspect_mmo_runtime_navigation.py` reads runtime SQLite waypoint/routine/navigation/NPC-relation evidence.
- `tools/inspect_mmo_mysql_server_bootstrap_state.py` reads MySQL current projections/procedures and reports restart/bootstrap gaps.
- `tools/run_mmo_step49_server_bootstrap_probe.py` runs both probes and writes JSON artifacts.

These are diagnostics, not server authority.

## Waypoints and NPC navigation

OpenGothic already captures runtime navigation shape in SQLite when the runtime DB has schema >= 11:
- `runtime_waypoints`
- `runtime_waypoint_edges`
- `runtime_npc_routines`
- `runtime_npc_navigation_state`
- `runtime_npc_navigation_history`

The server can temporarily read this as a bootstrap oracle to answer:
- which waypoints exist,
- whether NPCs currently have/reroute to routine waypoints,
- whether move target/path-next waypoint data changes over time.

This still does not mean server-authoritative AI is done. For authority, the MMO server needs:
- deterministic server tick,
- NPC routine scheduler,
- path query over waynet,
- replicated NPC movement snapshots,
- script-side validation for routine callbacks and AI state changes.

## NPC -> NPC actions

NPC->NPC conversations, reactions and combat are not owned by the second NPC "detecting an action packet" in the MMO model.
The authoritative server should own a single interaction transaction:

```text
actor_npc action -> validate target_npc / AI state / distance / schedule -> append semantic event -> update both projections -> replicate result
```

The target NPC can have perception/AI hooks, but DB truth should be one accepted server mutation, not two clients racing.
Useful current evidence fields:
- `runtime_npc_ai_state.state_other_key`
- `runtime_npc_ai_state.state_victim_key`
- `runtime_npc_relation_checkpoints`

## Chapters and quest stages

Chapter progress is durable script state, primarily `KAPITEL`, plus story-progress projections.
The correct event is a script/progression transaction, not a UI chapter banner. The UI `IntroduceChapter(...)` is presentation evidence only.

Quest stages are already represented by quest key/status/entry count and script vars. Keep both:
- human quest projection for UI/status,
- exact script globals for Gothic one-shot and branch checks.

## Teleports / world transitions

Teleport is not normal movement. It should be a server-authorized `character_world_transition` or `character_teleport` transaction:
- validate source trigger/spell/script,
- update character world instance / position atomically,
- generate checkpoint with transition reason,
- load interest area around destination,
- prevent replay with idempotency key.

Do not trust raw movement proposal for teleport distances.

## Digging / buried items / Greg-style tasks

Digging is a semantic world interaction:

```text
use_tool_or_interactive -> script var / quest condition -> spawn or reveal item -> character pickup/container/world projection
```

It needs a durable one-shot key, otherwise players can relog/retrigger and duplicate buried rewards.

## Server restart and login after restart

On restart, the server should materialize runtime state from current projections, not from baseline:
- character position/stats/wallet/inventory/equipment,
- quest/dialog/script/story progress,
- world entity state for NPCs/interactives/items,
- item instance ownership and lifecycle,
- container inventories.

If a player picked a world item, that item must not be spawned from baseline again. If a player took an item from a chest, that item must now be character-owned and absent from the chest projection.

Baseline is only template/oracle. Current projection + event journal is live truth.

## Respawn

Future respawn must be explicit scheduled server events, not login reset:
- `mmo_respawn_world_item(...)` for plants/loose world items,
- `mmo_respawn_container_item(...)` for selected container loot,
- respawn policy per item/template/location/container,
- event journal row + projection update + idempotency.

NPC respawn already has an existing DB shape through `mmo_respawn_npc(...)`; item/container respawn still needs canonical procedure support.

## Learning / spending learning points

XP/LP grants are currently covered by progression actions. Spending LP on a teacher is different:

```text
dialog/teacher option -> validate trainer + required LP/gold/conditions -> decrement learning_points -> increase talent/stat -> journal event
```

Do not model learning as only `set_script_int` or only stat delta. It must be an atomic training transaction because it consumes LP/gold and changes stats/talents/script gates.

Missing target procedure to add later:
- `mmo_spend_learning_points(...)`

## Step49 acceptance

Read-only acceptance:
- runtime probe shows waypoint/routine/navigation/NPC relation counts if SQLite has them,
- MySQL probe shows current projection tables available for restart materialization,
- missing procedures are listed explicitly instead of being treated as passed.

This step does not claim full server authority.
