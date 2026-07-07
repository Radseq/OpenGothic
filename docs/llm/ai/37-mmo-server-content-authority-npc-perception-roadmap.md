# 37 MMO Server Content Authority And NPC Perception Roadmap

Goal: make the MMO server the only authority for NPC perception, reactions,
dialog initiation, routine movement and script-driven world decisions while the
OpenGothic client stays a presentation/input terminal.

This document describes the future content-loading and server-authority path for
cases such as "NPC starts talking to a player". The important rule is that the
server must not trust files from a player's machine. The server owns a validated
content pack for the exact game/mod revision and clients only prove that they
match it.

## Current Client Shape

In the current OpenGothic client, an NPC approaching or talking to the player is
not a single dialog packet. It is the result of several Gothic systems:

- world and object state loaded from ZEN/save;
- NPC runtime state, position, senses, routine and current AI state;
- active/passive perception dispatch;
- Daedalus functions assigned to `PERC_*` slots;
- AI commands pushed into `AiQueue`;
- dialog/output playback as the visible result.

Important client-side flow:

```text
WorldObjects::sendPassivePerc / sendImmediatePerc
-> WorldObjects::passivePerceptionProcess
-> Npc::perceptionProcess
-> GameScript::invokeState
-> Daedalus B_Assess*/ZS_* function
-> AI_* intrinsic
-> Npc::aiPush(AiQueue::...)
-> animation / movement / dialog / combat
```

Important functions and areas in the current code snapshot:

- `Npc::perceptionProcess(Npc& pl)` actively checks `PERC_ASSESSPLAYER`,
  `PERC_ASSESSENEMY` and `PERC_ASSESSBODY`.
- `Npc::perceptionProcess(Npc&, Npc*, float, PercType)` validates perception
  range with `GameScript::percRanges()` and invokes the assigned perception
  function.
- `WorldObjects::passivePerceptionProcess` handles passive events such as theft,
  damage, sound, entering rooms and mob use.
- `GameScript::ta_min` builds NPC routines.
- `Npc::tickRoutine` starts routine states or initial AI states.
- `GameScript::ai_turntonpc`, `ai_gotonpc`, `ai_gotowp`, `ai_gotofp`,
  `ai_outputsvm`, `ai_processinfo`, `ai_attack`, `ai_flee`, `ai_startstate`
  and similar intrinsics push local AI queue actions.
- `RecordNpcDialogLine` is currently useful observation/debug evidence, not a
  final authority source.

## Current Server/DB Base

The project already has useful server-authority foundations:

- `content_revisions` with hashes for script/world/item/NPC content.
- `content_world_templates` for world template metadata.
- `content_entity_templates` for imported NPC/creature/item/interactive/etc.
  templates.
- `world_entity_state` for persistent world entity lifecycle/position/HP.
- `mmo_server_waypoint_read_model` and `mmo_server_waypoint_edge_read_model`.
- `mmo_npc_routine_state_current/history`.
- `mmo_npc_ai_state_current/history`.
- `mmo_npc_path_state_current/history`.
- `mmo_npc_fight_state_current/history`.
- `mmo_npc_reaction_history`.
- `mmo_record_npc_reaction_started`.
- `mmo_record_npc_dialog_initiated`.
- server-side perception modules:
  - `mmo_server_perception_authority`;
  - `mmo_server_perception_queue`;
  - `mmo_server_perception_sensor`;
  - `mmo_server_perception_reaction_planner`;
  - `mmo_server_perception_witness_registry`.

This means the project is no longer at "nothing exists". The missing part is
turning content files into server-owned runtime rules and replacing client-side
NPC AI observation with server-side simulation and replication.

The code snapshot exposed one immediate transport gap:
`queuePerceptionReactionLiveDelta` already assigns
`ServerLiveDeltaKind::PerceptionReaction`, but the shared live-delta enum did not
reserve that domain yet.

Step37 implementation note:

- `ServerLiveDeltaKind::PerceptionReaction` is now reserved in the shared
  network protocol as the typed live-delta domain for authoritative NPC
  perception reactions.
- The client-side live-delta debug/domain mapper names it `npc_perception`.
- This is intentionally only a transport/domain closure. It does not make the
  client authoritative for NPC decisions; it gives the server-side perception
  planner a typed channel to replicate its decisions.

## Authority Rule

The final production rule should be:

```text
server content pack + DB current projections
-> server world memory
-> server NPC perception/reaction/script/AI tick
-> durable event + projection update
-> live delta to interested clients
-> client presentation only
```

The server does not read ZEN/DAT/OU from a player's machine. The server owns its
own content pack:

- ZEN worlds;
- DAT/Daedalus scripts;
- OU/dialog output metadata;
- SVM/output mappings;
- waynet/freepoint data;
- guild attitude tables;
- collision/line-of-sight/navigation data needed for validation;
- a manifest/hash for the exact revision.

The client sends only a content revision/hash during login/bootstrap. If the
client's content does not match the server's active revision, the session must
be rejected or placed into a non-authoritative diagnostic mode.

## Content Import Roadmap

### Phase 1: Content Manifest

Create a deterministic server content manifest for every supported game/mod
revision.

Required hashes:

- `scripts_hash`;
- `worlds_hash`;
- `items_hash`;
- `npcs_hash`;
- `dialogs_hash`;
- `outputs_hash`;
- `waynet_hash`;
- `collision_hash`;
- `migration_hash`.

The DB already has `content_revisions`; extend the importer/read model so these
hashes represent actual server-owned content, not only runtime capture evidence.

Client bootstrap should include:

- game target, e.g. `g2notr`;
- content revision key;
- client content hash;
- protocol version;
- optional mod identifier.

Server bootstrap should validate those values before accepting gameplay.

### Phase 2: ZEN Import

The server importer must extract from ZEN:

- world name/key;
- static VOBs relevant to gameplay;
- NPC/creature/world item spawn references;
- mobsi/interactives;
- triggers, movers, doors, damage volumes and world start triggers;
- waypoints and freepoints;
- waypoint edges and distances;
- start positions and named points;
- enough collision/visibility data for line-of-sight and reachability checks.

Initial DB targets:

- `content_world_templates`;
- `content_entity_templates`;
- `mmo_server_waypoint_read_model`;
- `mmo_server_waypoint_edge_read_model`;
- future typed `content_interactive_templates`;
- future typed `content_trigger_templates`.

Runtime targets:

- `WorldRuntime.staticVobs`;
- `WorldRuntime.wayGraph`;
- `WorldRuntime.spatialIndex`;
- `WorldRuntime.interactiveIndex`;
- `WorldRuntime.triggerIndex`.

### Phase 3: DAT/Daedalus Import

The server must know script definitions, not just observed client results.

Extract:

- symbol table names/indices;
- item templates and item flags;
- NPC templates: guild, true guild, attributes, protection, talents, senses,
  senses range, start waypoint, start AI state, body state, inventory;
- perception function assignments per NPC/template;
- perception time/range tables;
- routine declarations from `TA_*`/`ta_min` usage;
- dialog infos: NPC, condition function, information function, permanent flag,
  important flag, trade flag, description/title;
- script globals and their scope classification;
- guild attitudes and crime/faction rules;
- selected whitelisted `B_*`, `ZS_*`, `C_*` semantics needed by the server.

Future typed content tables/read models:

- `content_script_symbols`;
- `content_ai_states`;
- `content_npc_templates`;
- `content_npc_perceptions`;
- `content_npc_routines`;
- `content_dialog_infos`;
- `content_dialog_choices`;
- `content_guild_attitudes`;
- `content_script_globals`;
- `content_script_function_effects`.

Keep raw payloads for diagnosis, but do not use JSON as the hot authority path.

### Phase 4: OU/SVM/Dialog Output Import

Extract:

- output name;
- subtitle text;
- audio file name;
- estimated duration;
- speaker mapping when available;
- SVM keys used by guards, warnings, crime reactions and ambient talk.

Future typed target:

- `content_output_units`;
- `content_svm_outputs`;
- `content_dialog_output_map`.

Server live deltas for dialog should carry output identity and timing, not trust
client-discovered subtitles as authority.

## Server Runtime Roadmap

### Phase 5: Shard Startup Materialization

On server startup or world-instance activation:

1. Load active `content_revision`.
2. Load immutable content into `ContentCache`.
3. Load `realm_world_instances`.
4. Build one `WorldRuntime` per active world instance.
5. Materialize current projections from DB:
   - `world_entity_state`;
   - `world_inventory`;
   - interactives/movers;
   - world clock;
   - world script state;
   - NPC routine/AI/path/fight current rows.
6. Build spatial indexes for NPCs, players, items, interactives and triggers.
7. Start authoritative server tick.

Do not run gameplay tick by querying MySQL per NPC. DB is durability and recovery;
server memory is hot runtime truth.

### Phase 6: Multi-player NPC Perception

NPC perception must scan all relevant entities, not one `PC_HERO`.

Candidate targets:

- player characters in AOI;
- other NPCs and creatures;
- dead/unconscious bodies;
- dropped items;
- theft/use-mob/crime events;
- combat events;
- magic cast events;
- sound events;
- room/zone/trigger events.

Perception validation should use:

- distance and vertical range;
- line of sight when required;
- hearing/seeing sense masks;
- focus angle/peripheral rules;
- guild/faction attitude;
- current NPC lifecycle;
- current AI state and interruptibility;
- current conversation/combat locks;
- perception cooldown;
- event priority.

Existing server modules are the correct starting point:

- `Perception::Queue` for coalescing and ordering;
- `Perception::WitnessRegistry` for nearby observed NPC/player state;
- `Perception::Sensor` for witness/sight/hearing checks;
- `Perception::ReactionPlanner` for first-pass reaction choice.

### Phase 7: Reaction Planning

The existing reaction kinds are a good first abstraction:

- `Ignore`;
- `Observe`;
- `QueueScript`;
- `Interrupt`;
- `Warn`;
- `SuspectCrime`;
- `CallHelp`;
- `StartCombat`.

They should evolve into concrete server commands:

- `turn_to_actor`;
- `approach_actor`;
- `warn_actor`;
- `start_dialog`;
- `queue_dialog_line`;
- `call_help`;
- `draw_weapon`;
- `start_combat`;
- `stop_current_action`;
- `resume_routine`;
- `ignore_with_cooldown`.

Every accepted reaction needs:

- `server_tick`;
- `world_instance_id`;
- `actor_npc_key`;
- `target_key`;
- `perception_id`;
- `reaction_kind`;
- `reason`;
- `content_revision_key`;
- idempotency key;
- durable journal event or current projection update.

Use `mmo_record_npc_reaction_started` and
`mmo_record_npc_dialog_initiated` as the initial DB evidence surface, but move
validation/orchestration into C++ server code.

### Phase 8: Server NPC Action Queue

The client `AiQueue` is the local model, not the final authority queue. The
server needs its own deterministic action model:

- `ServerNpcIntent`;
- `ServerNpcAction`;
- `ServerNpcPathTask`;
- `ServerNpcConversation`;
- `ServerNpcCombatTask`;
- `ServerNpcRoutineTask`.

Do not persist animation frame or transient queue internals as truth. Persist
important state transitions and current projections:

- reaction started;
- dialog started;
- current speaker/listener;
- combat started;
- routine state;
- path state/checkpoint;
- HP/lifecycle;
- position checkpoint;
- action lock ownership.

### Phase 9: Script Execution Strategy

Do not try to port all Daedalus at once.

Recommended progression:

1. Declarative server mapping for the most important reactions:
   - `PERC_ASSESSPLAYER`;
   - `PERC_ASSESSTALK`;
   - `PERC_ASSESSTHEFT`;
   - `PERC_ASSESSDAMAGE`;
   - `PERC_ASSESSMURDER`;
   - `PERC_ASSESSWARN`;
   - `PERC_DRAWWEAPON`;
   - `PERC_ASSESSMAGIC`.
2. Whitelist selected `B_Assess*` and `ZS_*` functions by name/symbol.
3. Implement a restricted server script VM or script interpreter facade.
4. Expand function coverage only when a gameplay test proves the need.

Critical MMO difference: Gothic uses global `self`, `other`, `victim`. On the
server this must become a per-event execution context:

- `self = reacting NPC`;
- `other = player/NPC/item/event actor`;
- `victim = affected NPC/player when applicable`;
- script state scope = character/world/server, explicitly classified.

Never share mutable `self/other/victim` across simultaneous players or NPC ticks.

### Phase 10: Replication To Clients

Clients should not send NPC lines or NPC AI results as truth. They should receive
server live deltas and play them.

Needed typed live deltas:

- `ServerNpcTransformDelta`;
- `ServerNpcActionDelta`;
- `ServerNpcReactionDelta`;
- `ServerConversationStarted`;
- `ServerDialogLine`;
- `ServerCombatStarted`;
- `ServerNpcLifecycleDelta`;
- `ServerWorldItemDelta`;
- `ServerInteractiveDelta`.

Interest management:

- send NPC transform/action deltas only to players in AOI;
- send private dialog UI state only to the addressed player;
- send visible/audible NPC output to nearby observers when appropriate;
- do not let one player's client locally decide that an NPC has started a
  world-authoritative conversation.

## DB Persistence Rules

Persist as durable truth:

- character state, stats, inventory, equipment, quests, known dialogs;
- character/world script state after explicit classification;
- `world_entity_state` lifecycle, position checkpoint, HP;
- world inventory and item instances;
- interactive/mover state;
- world clock;
- NPC current routine/AI/path/fight/action/conversation projections;
- important reaction/dialog/combat events;
- action locks/cooldowns that affect gameplay after reconnect/restart.

Do not canonically persist:

- animation frame;
- render pose;
- local focus/camera/input;
- particles/audio;
- transient perception queue;
- transient path queue internals;
- temporary AI target if it can be reconstructed from current action;
- client-local UI state.

## Future Database Rewrite

The current MySQL schema is a production-shaped authority bridge, not the final
MMO database. It is valuable because it proves domains, identity rules,
idempotency, restore behavior and event/projection contracts while the server is
still being migrated. It should not be treated as the permanent hot-path design.

The final database should be rewritten around the actual MMO server runtime:

- C++ server owns validation, transaction orchestration and gameplay decisions.
- DB stores durable events, current projections and recovery checkpoints.
- Hot gameplay paths use typed indexed columns, not JSON payload scanning.
- Stored procedures become compatibility/migration/admin helpers, not the main
  gameplay engine.
- SQL views remain audit/admin surfaces, not authority dependencies.
- Large bootstrap JSON snapshots are replaced by typed read models and typed
  replication streams.
- Runtime-only data stays in server memory and is checkpointed only when it has
  durable gameplay meaning.

The expected direction is:

```text
current bridge schema
-> clearer typed read models
-> server-owned transaction modules
-> reduced procedure/view reliance
-> final MMO persistence schema
```

This future rewrite should be done after the important gameplay domains are
known from evidence: movement, inventory, combat, dialog/story, interactives,
world items, NPC lifecycle, NPC perception/reaction and routine/path authority.
Rewriting too early would freeze the wrong abstractions; rewriting too late would
leave performance and correctness tied to bridge debt.

## First Practical Vertical Slice

The safest next implementation slice:

```text
client observed or server-generated perception event
-> server validates source/target/world/content revision
-> server evaluates witnesses/range/cooldown
-> server creates ReactionPlan
-> server records mmo_record_npc_reaction_started
-> server sends typed live delta
-> client rotates/animates NPC and logs presentation
```

For the first slice, do not execute full Daedalus yet. Implement one guarded
case, for example:

- NPC sees player in range;
- NPC is active/alive/not already talking/fighting;
- NPC has `PERC_ASSESSPLAYER` or configured greet behavior;
- server plans `Warn` or `QueueScript`;
- server sends reaction delta;
- client only presents turn/look/output.

Then expand:

1. `start_dialog` with selected dialog info key.
2. `output_line` from server-owned OU/SVM mapping.
3. `approach_actor` using waypoint/freepoint navigation.
4. hostile reaction and `start_combat`.
5. crime witness propagation.
6. routine resume and cooldown handling.

## Success Criteria

The server can be considered the authority for NPC player interaction when:

- a clean server can start from content pack + DB without relying on native
  client `.sav` as truth;
- two players near the same NPC observe the same server-owned NPC state;
- only one player can own an exclusive NPC conversation lock;
- NPC reaction persists/replays correctly across reconnect/restart when needed;
- client-side `RecordNpcDialogLine` is diagnostic only;
- mismatched client content revision is rejected;
- server can explain each reaction by content revision, perception id, NPC state,
  target, witnesses and script/rule path.

## Near-term Code Notes

- Keep `-mmo-client-server` as the global opt-in for server-bound behavior.
- Continue splitting focused modules out of `mmo_udp_server.cpp`.
- Do not make production gameplay depend on Python worker/outbox paths.
- Prefer typed packets and typed C++ authority modules over JSON hot paths.
- Treat current client NPC observation packets as training/diagnostic evidence
  until server-owned AI is implemented.
- Keep stable identity strict: world, persistent id, symbol/script id, template
  id, DB UUID. Do not match gameplay authority by display name alone.
