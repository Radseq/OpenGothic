# 03 Code Map And Hooks

Main C++ files:
- `game/commandline.cpp/.h`: MMO flags.
- `game/game/gamesession.cpp/.h`: new/load session, tick, server snapshot restore/wait/dirty-story guard,
  HERO/world snapshot materialization, movement proposal/checkpoint cadence.
- `game/game/mmosemanticevents.h/.cpp`: semantic action definitions and legacy
  JSON evidence serialization.
- `game/game/mmosemanticactionsink.cpp/.h`: async semantic action sink. In
  server-bound mode it sends binary UDP packets through ASIO.
- `game/game/mmosemantichooks.cpp/.h`: gameplay hooks that emit semantic
  actions.
- `game/game/mmoruntimesqlite.cpp/.h`: local runtime SQLite capture/restore.
- `game/game/mmorestoresnapshot.h`: guarded MySQL restore snapshot consumption, including HERO, story, world item deltas, active world items, interactive state and NPC lifecycle state.
- `game/game/worldstateexporter.cpp/.h`: structured state export history.

New game:
- `GameSession::GameSession(std::string file)` creates `GameScript`, loads
  `World`, creates HERO, runs `postInit`, `initScripts(true)`,
  `triggerOnStart(true)`, resets camera and sets `ticks=1`.
- Pre-Xardas baseline capture uses `-mmo-sqlite-capture-pre-start-exit`, opens
  SQLite and flushes before world start triggers.

Save/load:
- `GameSession::GameSession(Serialize& fin)` loads a save.
- `GameSession::save(...)` writes header, preview, session, camera, visited
  worlds, active world, perceptions, quests and Daedalus variables.
- `.sav` remains compatibility/debug backup for MMO mode.

World/NPC/item persistence context:
- `World::save/load` and `WorldObjects::save/load` contain NPCs, invalid NPCs,
  items, root VOBs/mobsi, trigger events and routines.
- `Npc::save/load` contains instance, visual, position, angle, guild, talents,
  attitude, perception functions, spell/combat state, transform state, AI state,
  victim/target, waypoint/path, movement/fight state, torch, physics, inventory.
- `Item::save/load` contains symbol index, item fields, amount, position,
  equipped/slot, transform.

Hook locations already important:
- movement/checkpoint: `GameSession::tick`, movement proposal helper.
- dialog/script/quest: `GameScript::exec`, `GameScript::invokeItem`,
  `GameScript::useInteractive`, dialog selection in `GameSession`.
- inventory/equipment: equip/unequip/use/drop should be captured at owner-aware Npc/Interactive boundaries; generic `Inventory::transfer` is not authoritative.
- combat/lifecycle: `Mmo::Hooks::onNpcAttributeChanged` emits `apply_world_entity_damage`; `onNpcLifecycleChanged` emits `mark_npc_dead`; dead-NPC inventory transfer emits `loot_npc_inventory`.
- world items/interactives: world item removal/pickup, container take, interactive state/use.

ASIO dependency:
- User added standalone ASIO at `thirdparty/asio/include/asio.hpp`.
- Client code includes either `<asio.hpp>` from include path or the relative
  project path `../../thirdparty/asio/include/asio.hpp`.
- Server CMake adds `../../thirdparty/asio/include`.

Flag rules:
- `-mmo-action-jsonl` remains diagnostic file evidence.
- `-mmo-action-udp` alone remains legacy diagnostic UDP.
- `-mmo-client-server host:port` / `-mmo-server-endpoint host:port` means real
  server-bound mode and binary ASIO UDP transport.
- No server-bound behavior without explicit flags.




Step79 hook note:
- `Npc::addItem(Interactive&)` emits `onContainerInventoryTaken`, producing `take_container_item` with a stable `mobsi:<world>:<slot>:<vob>:<focus>` owner key.
- `Inventory::transfer` still exists for gameplay mechanics, but the generic MMO transfer hook is suppressed because it lacks target-owner identity.




Step80 code-map note:
- `QuestLog::mergePreservingLocal`, `GameScript::mergeQuestLogForPersistence` and `GameScript::mergeKnownDialogsForPersistence` exist to prevent stale bootstrap snapshots from wiping local dialog/quest changes.
- `server/cpp/mmo_server_snapshot_limits.h` and `server/cpp/mmo_server_types.h` are the first extraction points from the monolithic UDP server file.




Step81 code-map note:
- `GameSession::tryApplyMmoServerSnapshotRestore` now also applies `world_item_deltas` and `interactive_state` after HERO/story restore while semantic capture is suppressed.
- World item deletion uses local `World::itmById` scan and matches persistent id plus optional symbol. Interactive restore resolves `World::mobsiById(slot)` and calls `Interactive::restorePersistentState`.




Step82 code-map note:
- `Mmo::RestoreSnapshot::WorldInventoryItem` parses the new `active_world_items` section.
- `GameSession::applyMmoWorldSnapshotState` now removes tombstoned baseline items, reports already-absent tombstones separately, and spawns/updates active DB world items via `World::addItem`.
- Snapshot world-item matching uses persistent id plus optional symbol first, with a conservative symbol+near-position fallback for save files whose local persistent ids differ.



Step83 code-map note:
- `server/cpp/mmo_udp_server.cpp` now contains a canonical NPC resolver used by damage/death/loot. It maps hook aliases such as `npc:<pid>:sym:<symbol>` and `npc:<world>:pid:<pid>:sym:<symbol>` to live `world_entity_state.entity_key`.
- `server/sql/step83_combat_lifecycle_bridge.sql` defines the clean-DB procedures required by those direct C++ combat handlers.




Step84 code-map note:
- `server/cpp/mmo_server_identity.h` contains focused canonical key helpers for NPC and world item resolver paths. Continue extracting resolver/session/snapshot code from `mmo_udp_server.cpp` instead of growing the monolith.
- `Npc::restorePersistentLifecycle` and `GameSession::applyMmoWorldSnapshotState` apply load-time NPC HP/dead state from `npc_lifecycle_state` while semantic capture is suppressed.
- `server/sql/step84_world_identity_lifecycle_bridge.sql` defines `mmo_grant_character_item_by_symbol`, the temporary unresolved-pickup grant fallback used by the C++ server.
