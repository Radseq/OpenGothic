# 06 Native Save to DB/Server Roadmap

Purpose: enumerate what the native `.sav` file currently preserves and define the
migration path to a server/DB-only runtime where `.sav` is only a compatibility
or debug fallback.

This document is based on the current OpenGothic save/load code path, especially:

- `game/mainwindow.cpp`: `startGame`, `loadGame`, `saveGame`.
- `game/gothic.cpp`: `startLoad`, `startSave`, `save`, `load` signal bridge.
- `game/game/serialize.*`: zip archive wrapper, versioning, primitive/vector/string/pointer serialization.
- `game/game/savegameheader.*`: native save header.
- `game/game/gamesession.*`: session constructor/load/save orchestration and MMO restore scheduling.
- `game/world/world.*`, `game/world/worldobjects.*`: per-world state.
- `game/world/objects/{npc,item,interactive,vob}.*`: main persistent world actors.
- `game/world/triggers/*`: trigger, mover and delayed event state.
- `game/game/{gamescript,questlog,inventory}.*`: Daedalus state, quest/dialog facts, inventory/equipment.

## Native save archive shape

The OpenGothic save is a miniz zip-like archive driven by `Serialize`. A save is
not a single struct; it is a directory of typed binary entries. Important current
entries are:

```text
header
preview.jpg
game/session
game/camera
game/perc
game/quests
game/daedalus
worlds/<world>.zip                     # previously visited world blob
worlds/<world>/world                   # active world portal + object state
worlds/<world>/version
worlds/<world>/npc/<slot>/data
worlds/<world>/npc/<slot>/visual
worlds/<world>/npc/<slot>/inventory
worlds/<world>/npc_invalid/<slot>/...
worlds/<world>/items
worlds/<world>/mobsi/<vob_id>/data
worlds/<world>/mobsi/<vob_id>/inventory
worlds/<world>/mobsi/<vob_id>/visual
worlds/<world>/triggerEvents
worlds/<world>/routines
```

`Serialize` converts in-memory pointers to local save identifiers such as NPC
slot id, mobsi id or waypoint name. This is acceptable inside one local save but
must not become production MMO identity. Server identity must remain stable:
character key, world/template revision, persistent id, symbol/script id, VOB id,
item template symbol and DB UUID.

## Current load flow

### New Game

`GameSession(std::string file)` currently:

1. Initializes camera, settings, script VM and perception tables.
2. Loads the ZEN world baseline.
3. Creates HERO from the script default player.
4. Initializes scripts and dialog definitions.
5. In server-bound mode requests the bootstrap snapshot before start triggers.
6. Calls `wrld->triggerOnStart(true)`.
7. Opens optional runtime SQLite capture/restore.
8. Applies the server restore snapshot.

For DB-only play, this path should stay the bootstrap path: load deterministic
content baseline, then apply DB/server state before start-trigger side effects.

### Load native save

`GameSession(Serialize& fin, sourceSlot)` currently:

1. Reads `header` and global save version.
2. Reads names of visited worlds and raw `WorldStateStorage` blobs.
3. Reads `game/session`: tick count, world time, world time fraction and current
   world name.
4. Loads the current world from ZEN, then overlays `wrld->load(fin)` from `.sav`.
5. Loads `game/perc`, `game/quests`, `game/daedalus`.
6. Binds `HERO` instance to the loaded player NPC.
7. Loads `game/camera`.
8. Opens optional runtime SQLite.
9. Requests and applies the server snapshot in server-bound mode.

For DB-only play, step 4 must stop using `.sav` as authority. The future path is:
load ZEN baseline only, then materialize DB/server slices. The local `.sav` can
remain a debug cache until every required slice has parity.

### Save native save

`GameSession::save` currently writes:

1. `header`: version, display name, current world, PC time, world time, playtime,
   Gothic version flag.
2. visited world list and each visited world raw `WorldStateStorage` blob.
3. `preview.jpg` screenshot.
4. `game/session`: tick count, world time, world time fraction, current world.
5. `game/camera`.
6. current `wrld->save`.
7. `game/perc`, `game/quests`, `game/daedalus`.

For MMO, explicit save should become a server checkpoint request, not a full
world dump from client memory. Preview/camera can remain local client state.

## Save-domain inventory and DB mapping

| Native save domain | Current native source | Current DB/server state | Roadmap status |
|---|---|---|---|
| Save metadata | `SaveGameHeader` | `server_sessions`, `characters`, future save/checkpoint manifest | Needed for DB-only continue/load slots. |
| Preview screenshot | `preview.jpg` | none | Client-only convenience; not authoritative. |
| Current world and visited worlds | `game/session`, `WorldStateStorage` | `characters.current_world_instance_id`, `character_positions`, `realm_world_instances`, teleport/world-transition events | Add typed character-world session manifest. Stop storing raw visited-world blobs as authority. |
| World clock/play time | `wrldTime`, `wrldTimePart`, `ticks` | `realm_world_instances.current_tick/current_world_time_ms`, `mmo_world_clock_state_current/history` | Existing bridge exists; needs end-to-end ownership and no local sleep advancement in server-bound mode. |
| HERO stats/resources | serialized `INpc` + attributes | `character_stats`, resource bridge, checkpoints | Mostly present; extend to talents, hitchance, protections, aivar and guild fields. |
| HERO position | `Npc::x/y/z/angle`, physics pos | `character_positions`, checkpoint audit | Present for load-time; needs server correction on rejected movement. |
| HERO inventory/equipment | `Inventory::save`, item instances, slots | `character_inventory`, `character_equipment`, `item_instances` | Present for main inventory/equipment; extend active slot, quick slots, ammo/state slots and exact equip counts. |
| Quest log | `QuestLog::save` | `character_quests` | Present; fix charset/idempotency issue for non-ASCII quest keys before DB-only play. |
| Known dialogs | `dlgKnownInfos` | `character_known_dialogs` | Present for known/consumed facts; full dialog availability still depends on condition functions and script state. |
| Daedalus globals | `game/daedalus` saves all non-member/non-const int/float/string and selected instance refs | `character_script_state`, `world_script_state` | Character int state is applied; classify world vs character vs ephemeral, add float/string/array support only where needed. |
| Guild attitudes | saved with quests | partially script/world state | Needs explicit typed projection, probably world or realm scoped. |
| Perception ranges | `game/perc` | no typed authority | Treat as content/script config unless runtime scripts change it; otherwise store as script state. |
| Portal/sector guilds | `World::save` portal sector data | no dedicated projection | Needed for crimes/ownership/guards only if gameplay uses mutable sector guilds. |
| Active/invalid NPC arrays | `worlds/<world>/npc` and `npc_invalid` | `world_entity_state`, `npc_lifecycle_state`, observed NPC bridge, nearby NPC window | Lifecycle exists; missing authoritative spawn/despawn/invalid lists and server-side NPC positions/routines. |
| NPC persistent identity | `npcPersistentId`, symbol index, local slot | `world_entity_state.entity_key`, `content_entity_templates`, DB UUID | Present but still uses observed materialization for runtime drift. Replace with deterministic content import + spawn policy. |
| NPC attributes | serialized `zenkit::INpc`, talents, attitudes, perception funcs | partial `world_entity_state.health_*`, lifecycle | Add typed NPC stat projection for HP, guild, level, attributes, protections, weapon state, attitude. |
| NPC inventory/equipment | `Npc::inventory` | `world_inventory`, item instances, corpse loot materialization | Corpse loot bridge exists; add baseline NPC inventories, equipment and loot-table authority. |
| NPC AI/routines | `saveAiState`, `AiQueue`, routine list, `WayPath`, current FP/lock | no server AI runtime yet; nearby NPCs are read-only snapshot | Do not persist raw queues as production truth. Build server runtime AI/routine engine, with optional crash-recovery checkpoints. |
| NPC movement/fight state | `MoveAlgo`, `FightAlgo`, current target/victim/look-at | not authoritative | Runtime-only. Server should simulate and stream corrections, not load client queues. |
| NPC visual/body/animation | body/head/visual, `MdlVisual`, pose/overlays | template/raw payload only | Persist durable visual variants/body choice when gameplay changes them; animation pose is client/runtime only. |
| World items | `worlds/<world>/items`, `Item::save` | `world_entity_state(entity_kind=item)`, `item_instances`, `world_inventory`, active item interest window | Nearby DB-authoritative item window exists; continue converting local/observed bridge to server-owned item lifecycle. |
| Item local transform | item pos + transform matrix | pos in `world_entity_state`, raw JSON | Add yaw/transform only where needed. Most world items can use pos + template visual. |
| Containers/interactives | `Interactive::save`, inventory, state, lock/crack, attach positions | `world_entity_state(interactive)`, `world_inventory`, `world_interactive_audit`, `interactive_state` snapshot | State/lock/crack load exists; complete container put/take/trade and DB-driven inventory materialization. |
| Mobsi/interactive visual state | `ObjVisual`/`MdlVisual` for mobsi | partial raw payload | Usually client-render state; persist only durable state id/lock/crack and server-owned inventory. |
| VOB transform/state | `Vob::save` type, pos, local matrix | `world_entity_state(vob/trigger/interactive)` | Persist only mutable VOBs/movers/triggers. Static ZEN baseline is content, not runtime DB truth. |
| Trigger delayed events | `worlds/<world>/triggerEvents`, `AbstractTrigger::delayedEvent`, ticks enabled | `mmo_world_trigger_events`; trigger bridge exists | Need DB materialization of pending delayed events and server-side trigger scheduler. |
| Movers/doors/gates | `MoveTrigger::state/frame/targetFrame` | `mmo_world_mover_state_current/history`; mover bridge exists | Need client load-time materialization of mover frames and live mover deltas. Fireplace/Xardas gate still mainly local world logic. |
| CodeMaster/TriggerList/PfxController | trigger subclass state | partial trigger/mover procedures | Persist only gameplay-affecting trigger state, not render-only particles. |
| Camera | `game/camera` | none | Client preference/cache only. Do not make gameplay depend on it. |
| Audio/particles/render/focus | some VOB/visual side effects | none | Do not persist as authoritative DB state. |

## What must not be copied 1:1 from `.sav`

The native save stores local continuation details that are harmful as MMO truth:

- raw C++ pointer relationships converted to local array ids;
- live AI queues, fight queues and animation pose;
- particles, audio, camera, render/focus details;
- transient local physics state that should be recomputed by server/runtime;
- full binary world blobs as a periodic authority source.

For production, keep durable facts and reconstruct runtime behavior from server
state: content revision + entity identity + position/lifecycle/inventory + script
state + event journal. Server memory may keep richer runtime objects, but DB should
not become a dump of client internals.

## DB-only roadmap

### Phase 0 - Fix blocking evidence and diagnostics

1. Fix non-ASCII quest/dialog/idempotency encoding in the C++ direct DB path.
   Recent evidence shows `update_quest` can fail on a Polish quest key with an
   incorrect-string-value error. DB-only story cannot tolerate rejected quest
   events.
2. Keep `BIN_TO_UUID(..., 1)` admin views for BINARY(16) ids. GUI `BLOB` display
   is expected; it is not bad data.
3. Add a save-parity audit tool that prints native-save domains vs current DB
   projections for one run: character, world items, NPC lifecycle, interactives,
   movers, triggers, quests/dialogs/script state.

### Phase 1 - DB session manifest instead of native save slot

Target: pressing Save writes/updates a server checkpoint manifest; Continue uses
server session/character state, not a required `.sav` file.

Needed server projections:

- current character world instance;
- last accepted server tick;
- current world time;
- current HERO position/resources;
- checkpoint label/display name and optional client preview path/hash.

Client behavior:

- `-mmo-client-server` starts from content baseline and server bootstrap.
- `.sav` remains optional local cache only.
- A missing/old `.sav` must not block a DB-backed character from loading.

### Phase 2 - Full HERO parity

Already present: position, stats, inventory/equipment, quests, known dialogs,
script ints.

Add or verify:

- talents, skills, hitchance, protection, guild/true guild, `aivar` values;
- active weapon/spell state only when durable enough;
- quickslot/active inventory slot, ammo/state slots, exact equipped stack counts;
- float/string/array script state only for classified gameplay globals;
- server correction after rejected movement/equipment/pickup actions.

Exit criterion: a new DB-only session can talk to Xardas/Maleth, loot, equip,
fight, save, restart without reading native HERO state.

### Phase 3 - Server-owned world items and containers

Already present: nearby active item window, tombstones, observed-item bridge,
corpse-loot materialization.

Add:

- typed item window deltas instead of repeated full JSON snapshot reuse;
- server-owned drop/spawn/despawn lifecycle for world items;
- container inventory materialization for nearby containers only;
- container put/trade/give/take flows with owner-aware validation;
- respawn policy for plants/loot/containers where Gothic scripts expect it.

Exit criterion: local `.sav` items inside the interest window can be discarded;
server data alone recreates all visible nearby items and container contents.

### Phase 4 - Interactives, movers and trigger scheduler

Already present: interactive use/state capture, lock/crack/state restore, trigger
and mover procedures as bridge evidence.

Add:

- load-time mover materialization from `mmo_world_mover_state_current`;
- live mover state delta packet for doors/gates/platforms;
- pending delayed trigger scheduler on server;
- trigger/codemaster state projection for gameplay-affecting triggers;
- DB restore of fireplace/Xardas-gate style mechanisms without relying on native
  save VOB state.

Exit criterion: using a switch, lever, lockpick or trigger-controlled gate stays
correct after restart without native `.sav` mover state.

### Phase 5 - NPC authoritative lifecycle and inventory

Already present: nearby NPC read-only window, NPC lifecycle restore, observed NPC
materialization, combat/death/loot direct DB paths.

Add:

- deterministic baseline NPC spawn set from content revision;
- replacement for observed materialization in normal clean DB flow;
- server-owned NPC inventory/equipment and loot rules;
- nearby NPC spawn/despawn materialization on client;
- NPC position/routine checkpoint read model;
- invalid/dead/disabled/despawned NPC state replacing native `npc_invalid`.

Exit criterion: killed/looted NPCs, spawned runtime NPCs and nearby NPC presence
are server facts, not artifacts recovered from client save/runtime drift.

### Phase 6 - Server AI/routines/pathing

This is the real MMO boundary. Do not fake it by dumping `AiQueue` from client to
DB.

Needed server systems:

- waypoint/edge import from ZEN/content revision;
- routine scheduler from Daedalus TA/routine definitions;
- server-side movement/pathing for idle/routine NPCs;
- perception/aggro/reaction model;
- combat state machine sufficient for PvE;
- interest management packets for NPC transforms and state changes;
- client correction/prediction policy.

Current evidence shows waypoint counts are still zero in the server readiness/logs,
so waypoint import/read-model must be fixed before NPC routines can be serious.

Exit criterion: NPCs can walk routines, react, fight and die with server as the
source of truth. Client becomes renderer/input/prediction, not authority.

### Phase 7 - Disable native save authority

When phases 1-6 are good enough:

- `Save` sends a server checkpoint request and optionally writes a local cache.
- `Load/Continue` logs in, receives bootstrap/interest windows and reconstructs
  from DB.
- `.sav` is only compatibility/debug/export/import.
- Native save load path remains unchanged without `-mmo-client-server`.

## Suggested implementation order after Step92

1. Fix `update_quest` UTF-8/idempotency path.
2. Add DB save-manifest/checkpoint endpoint and tool to inspect it.
3. Add load mode that starts from ZEN baseline + DB snapshot even when no native
   `.sav` is provided.
4. Move snapshot/read-model SQL out of `mmo_udp_server.cpp` into focused modules.
5. Replace live full JSON snapshots with typed binary deltas for item/NPC windows.
6. Materialize mover state from DB on client load.
7. Fix waypoint import/read-model; then start server NPC routine prototype.

## Non-goals for the next patches

- Do not rewrite the old native save system.
- Do not remove `.sav` compatibility.
- Do not persist raw AI queues as final MMO truth.
- Do not make game thread call MySQL directly.
- Do not infer production authority from periodic full-world diffs.
