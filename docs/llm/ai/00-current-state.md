# 00 Current State

Goal: turn OpenGothic/Gothic II NotR into a server-authoritative MMO while
native `.sav` remains compatibility/debug backup until DB restore parity is
proven.

Current working loop:

```text
OpenGothic client -> ASIO UDP binary packet -> C++ ASIO UDP server
-> MySQL procedures/read models -> world_event_journal + current projections
```

The previous Python receiver/worker path is now fallback/debug debt. Do not make
new gameplay work depend on starting a worker process. The current dev target is:

```text
OpenGothic -mmo-client-server -> ASIO UDP binary packet -> C++ ASIO UDP server
-> direct DB procedure call -> binary ACK/NACK
```

Bootstrap restore status:
- On `client_bootstrap_request`, the C++ server now sends a binary bootstrap
  ACK and then a chunked `mmo_bootstrap_snapshot_v1` JSON snapshot back to the
  client over the same UDP socket.
- The client writes the server snapshot to
  `runtime/mmo_server_bootstrap_snapshot.json`.
- In server-bound mode (`-mmo-client-server`), the client applies the downloaded
  server snapshot to HERO after load: stats/resources, inventory/equipment,
  position, quest log, known dialogs, full character integer script state when it is safe, world item tombstones, active server-owned world items, interactive/mobsi state and NPC lifecycle slices. No per-domain apply flags are required in the normal flow.
- Without `-mmo-client-server`, old `.sav`/New Game state remains untouched.

Important status:
- C++ semantic action hooks exist and are disabled by default.
- `-mmo-client-server host:port` opts into server-bound behavior and emits
  `client_bootstrap_request` after new/save session load.
- Server-bound sleep/rest does not advance local world time; it restores local
  HP/mana only. Server-owned world time remains future work.
- Clean local testing uses `PC_HERO_TEST` with session
  `local-dev-PC_HERO_TEST`.
- `runtime/g2notr_ch1_pre_xardas.sqlite` is the repeatable New Game capture
  before Xardas/start-trigger side effects. Do not remove this reset path.
- `run_mmo_step55_clean_mysql_from_pre_xardas.py` remains the destructive clean
  MySQL rebuild path for starting a new game/server test.
- The C++ UDP server must tolerate repeated clean DB rebuilds during local
  development. It validates its cached `server_sessions.session_id` and calls
  `mmo_login_character` again when the DB was dropped/recreated.

Current DB truth:
- Event journal + current projections are truth.
- Audit tables, logs, JSONL, manifests and snapshots are evidence only.
- The current MySQL schema is a production-shaped dev authority bridge, not the
  final production MMO database.
- Step53-style physical `mmo_server_*_read_model` tables are the preferred
  migration target for server bootstrap/materialization. Runtime views and JSON
  payload columns are bridge debt, not final hot-path design.

Domains with meaningful server/DB path:
- bootstrap ACK/read-model check in the C++ ASIO server;
- cached DB session self-heal after destructive clean MySQL rebuilds;
- bootstrap readiness falls back to live tables (`server_sessions`,
  `world_entity_state`, inventory/quest/dialog/script tables) when Step53
  read-model tables are present but empty after a clean rebuild;
- server-produced bootstrap snapshot download from live MySQL tables to
  `runtime/mmo_server_bootstrap_snapshot.json`;
- automatic server-bound client apply of bootstrap snapshot stats/resources, inventory/equipment, HERO position, quest log, known dialogs, safe full character script ints, world item tombstones, active server-owned world items, interactive/mobsi state and NPC lifecycle state after bootstrap;
- direct C++ `character_checkpoint -> mmo_checkpoint_character_state`;
- direct C++ movement proposal validation/checkpoint for normal walking slices;
- direct C++ dialog/script-int/quest/progression calls;
- direct C++ resource delta/mana consume, trigger and mover calls;
- direct C++ interactive use/state calls;
- direct C++ pickup/remove/drop/transfer/loot item and equip/unequip character
  item calls;
- direct C++ ready/holster weapon calls;
- direct C++ combat/lifecycle calls for character damage, NPC/world entity damage and NPC death.

Step75 notes:
- A ready bootstrap ACK does not prove snapshot delivery. The server must also
  print `bootstrap_snapshot_sent`. A failing snapshot build is a C++ server SQL
  bug, not a client protocol problem.
- `realm_world_instances` does not have `world_name`; snapshot SQL must join
  `content_world_templates` or use `world_instance_key`.
- Client now accepts `ServerDiagnostic` packets so rejected ACK/NACK lines have
  a server reason.
- `movement_proposal` can be emitted while drawing/holstering a weapon because
  the movement hook is periodic/tick-based. Tiny stale movement deltas after
  idle/script/weapon gaps should pass; impossible spatial movement should fail.

Outbox status:
- `mmo_server_action_outbox` remains an explicit debug/fallback path only.
- Use `mmo_udp_server --enqueue-outbox` only when comparing the old resolver
  behavior or debugging an unhandled domain.

Incomplete authority:
- client consumes and logs live ACK/NACK responses but does not yet rollback or
  correct local state after a rejected action;
- server snapshot restore is now automatic in server-bound mode, but still load-time
  materialization rather than real-time replication;
- server-produced bootstrap snapshot applies HERO stats/resources, inventory/equipment, position, quest log, known dialogs, safe full character script ints, world item tombstones, active server-owned world items, interactive/mobsi state and NPC lifecycle state after load; NPC movement/routines/world-inventory samples remain downloaded-only;
- NPC AI/pathing/live movement replication is future work; the server does not yet simulate or broadcast routine NPC movement;
- teleport/world transition/chapter transition must be separate events, not
  movement bypasses;
- trigger/mover/world-time/resource/training/respawn/NPC reaction procedures
  exist as contract/proof but still need end-to-end gameplay evidence.
- `loot_npc_inventory` has direct C++ owner-aware resolution for canonical NPC/world inventory keys; container put/trade remain future work.

Hard rules:
- Never design production as game thread -> MySQL.
- No accepted gameplay mutation without server validation and one durable event.
- Do not infer authority from periodic full-world diffs; diffs are validation.
- Stable identity only: character key, world/template revision, persistent id,
  symbol/script id, VOB id, item template symbol, DB UUID. Display names are
  labels.
- Do not persist pointers, live AI/path/fight queues, animation pose/frame,
  particles, audio, render/camera/input/focus.

Step76 notes:
- Server log can show hundreds of accepted direct DB actions while the client
  prints only a few ACK lines because normal accepted generic ACKs are now
  intentionally summarized, not printed one-by-one.
- Client UDP receive must be drained even while the semantic action queue is
  idle. Snapshot chunks can arrive after the bootstrap ACK because MySQL snapshot
  construction takes time.
- Client writes both `runtime/mmo_server_bootstrap_snapshot.json` and
  `runtime/mmo_server_bootstrap_snapshot_manifest.json` after complete chunk
  reassembly. Missing manifest means snapshot download was incomplete or never
  arrived.



Step78 notes:
- `-mmo-client-server` is now the single global switch for client materialization
  from server truth. Do not add per-domain apply flags for every snapshot section.
- GameSession schedules server snapshot restore after `client_bootstrap_request`
  and polls until the UDP sink writes `runtime/mmo_server_bootstrap_snapshot.json`.
- Applying the server snapshot intentionally replaces HERO stats/resources,
  inventory/equipment and position loaded from `.sav`/New Game with current DB
  truth.
- Server snapshot materialization suppresses semantic action capture while it is
  mutating local HERO state, so restore-time equip/set-position does not echo
  back as new gameplay intents.
- The C++ server item resolver treats Gothic persistent id `4294967295` as the
  local `-1` sentinel and falls back to resolving active character inventory by
  item template/symbol.






Step79 notes:
- Generic `Inventory::transfer` is no longer treated as an authoritative server intent because it lacks source and target owner identity. Domain-specific hooks must be used instead.
- Taking items from an `Interactive` container now emits `take_container_item` with `source_entity_key`/`container_key` equal to the stable mobsi key.
- The C++ server applies `take_container_item` through the existing world-inventory loot bridge (`mmo_loot_npc_inventory`) using the owner key plus item symbol resolver.
- Legacy `transfer_character_item` packets without both source and target character identity are accepted as no-op compatibility packets to avoid NACK spam from local inventory UI churn.
- Server-bound materialization now also applies quest log and known dialog state from `mmo_bootstrap_snapshot_v1`; full script-state apply is still future work because the current snapshot carries only a bounded script sample.




Step80 notes:
- Server-bound New Game requests and waits briefly for the bootstrap snapshot before `triggerOnStart(true)`, so DB materialization can happen before Xardas/start-trigger dialog AI begins.
- Late bootstrap snapshots must not overwrite local story/dialog changes made after the bootstrap request. GameSession marks story dirty on dialog exec; story restore then merges known dialogs/quest entries and skips script-int apply instead of replacing local state.
- Bootstrap snapshot now carries full character integer script state as `script_state` up to the server snapshot limit, not only `script_state_sample`. The client applies it only when no local story change happened since the request.
- Server monolith split has started with `server/cpp/mmo_server_snapshot_limits.h` for snapshot/chunk limits. Continue extracting focused server modules instead of growing `mmo_udp_server.cpp`.




Step81 notes:
- Bootstrap snapshot now has explicit `world_item_deltas` and `interactive_state` sections. Legacy aliases `world_entity_delta_sample` and `interactive_sample` are still emitted for compatibility.
- In server-bound mode the client applies removed/consumed/disabled item deltas by removing matching local world items by persistent id and optional symbol.
- In server-bound mode the client applies interactive/mobsi state by stable mobsi slot id parsed from `mobsi:<world>:<slot>:...` and restores state id, locked and cracked flags when present.
- This is still load-time world materialization, not live replication. NPC positions, routines, AI/path queues and full world inventory creation remain future work.
- Server C++ monolith split continues: shared server option/readiness/type structs live in `server/cpp/mmo_server_types.h`; snapshot constants live in `server/cpp/mmo_server_snapshot_limits.h`.




Step82 notes:
- Bootstrap snapshot size was reduced by emitting slim world item and interactive projections instead of full `state_json` payloads and duplicated legacy samples.
- `active_world_items` is now the server-facing snapshot section for active DB-owned world items such as dropped/spawned items. `world_inventory_sample` is only a compatibility alias for that filtered section, not a full container inventory dump.
- Client world materialization now distinguishes removed tombstones that were already absent in the native save from invalid/skipped deltas. A second launch after a save can therefore report `already_absent_world_items` instead of a misleading missing count.
- Client can spawn or update active DB world items at their server position by item symbol, amount and stable world item key. NPC positions/routines remain future work.



Step83 notes:
- Killing a creature/NPC now has a direct DB path: `apply_world_entity_damage` calls `mmo_apply_world_entity_damage`, and `mark_npc_dead` calls `mmo_mark_npc_dead`. Redundant local damage after the entity is already dead is accepted as a no-op instead of becoming a NACK.
- `loot_npc_inventory` canonicalizes source NPC aliases such as `npc:<pid>:sym:<symbol>` to the persisted `world_entity_state.entity_key` before resolving `world_inventory`. This fixes duplicate JSON key/alias issues from `appendNpcIdentity`.
- Clean DB rebuilds should install `server/sql/step83_combat_lifecycle_bridge.sql` by default through `run_mmo_step55_clean_mysql_from_pre_xardas.py` / `reset_mmo_mysql_from_chapter1_start.py`.





Step84 notes:
- The C++ server identity resolver now accepts canonical hook keys, DB keys and older malformed local aliases such as `npc.zen:pid:<id>:sym:<symbol>` and `world-item.zen:pid:<id>:sym:<symbol>`. The resolver fills the authoritative world from the session/payload and can fall back to symbol plus near target position for NPC combat hits.
- Bootstrap snapshot now carries `npc_lifecycle_state` for dead/damaged/disabled NPCs and creatures. The client applies only load-time HP/dead lifecycle state in server-bound mode; it still does not move active NPCs or restore transient AI/path/fight queues.
- `mmo_grant_character_item_by_symbol` is a temporary safe bridge for unresolved locally-created world pickups, such as corpse meat generated by the Gothic client before full server-owned corpse/drop spawning exists. It journals a durable inventory grant instead of returning a NACK.
- Live NPC/mob movement replication is still not implemented. Server currently sends ACK/NACK/diagnostics plus bootstrap/materialization snapshots, not per-tick NPC AI updates.

Step85 notes:
- NPC combat identity resolution now also checks `creature:` key variants and joins `content_entity_templates` for template `symbol_index`/`script_id`. This covers save/load cases where the hook sends a stable local `pid/sym`, but the clean DB imported the creature under a different entity key shape.
- Fuzzy NPC resolution remains bounded: it requires the same NPC/creature symbol or template id and a near payload position such as `target_position` or `source_npc_position`. Do not loosen this into display-name-only matching.
- `loot_npc_inventory` keeps strict DB resolution for containers, but corpse loot can use the same temporary `mmo_grant_character_item_by_symbol` bridge when the local client generated a corpse drop that is not yet server-owned inventory.

Step86 notes:
- Combat/death handlers can materialize an observed runtime NPC/creature when no matching `world_entity_state` row exists. This is for local save/runtime-spawned entities such as goblin groups with drifting persistent ids; it creates a minimal active world entity from stable hook fields (`world`, `pid`, `symbol`, position, HP, display label) and journals `world_npc_observed` before applying damage/death.
- Observed NPC materialization is a dev authority bridge, not live NPC AI. It lets DB lifecycle catch up to local save/runtime entities, but the server still does not simulate routines, aggro, waypoint movement or combat decisions.
- Corpse meat/local drop fallback remains temporary until server-owned corpse/drop spawning exists.

Step87 notes:
- A fresh New Game test that looted tower items, killed/looted sheep, goblin and young wolf, observed a multi-goblin fight, saved and reloaded produced clean server-bound evidence: first run `rejected=0 diagnostics=0`, reload applied inventory/equipment, HERO stats/position/story, `world_item_deltas=7` and `npc_lifecycle_state=2` with `applied_npc_lifecycle=2`.
- The remaining visible bridge was corpse loot from locally generated dead NPC inventory: it previously used `mmo_grant_character_item_by_symbol` and logged `[npc_loot_grant_fallback]` even when the action was accepted.
- Step87 makes corpse loot first materialize a deterministic observed item instance under the dead NPC's `world_inventory`, journals `world_npc_loot_observed`, then runs normal `mmo_loot_npc_inventory`. The direct grant remains only as last-resort fallback if source owner/template/materialization fails.
- This is still not server-owned corpse/drop spawning from AI simulation. It is a safer DB materialization bridge for locally observed corpse loot until the server owns NPC death/drop generation end to end.

Step88 notes:
- Step87 initially wrote observed corpse-loot events with `event_class='world_inventory'`, which violates `world_event_journal_event_class_ck`. Use allowed event classes such as `inventory` or `world_entity`; observed corpse loot now journals `world_npc_loot_observed` as `inventory`.
- `pickup_world_item` now has an observed-item bridge similar to observed NPC/corpse loot. If a locally present world item cannot be resolved in DB by `world-item:<world>:pid:<pid>:sym:<symbol>`, the C++ server creates deterministic `item_instances`, `world_entity_state(entity_kind='item')`, `world_inventory`, journals `world_item_observed`, then calls normal `mmo_pickup_world_item`. `mmo_grant_character_item_by_symbol` remains only as last-resort fallback.
- The fireplace/door mechanism is still local Gothic world logic: scripts/mobsi emit `Wld_SendTrigger` or an interactive trigger target, `World::triggerEvent` queues it, `WorldObjects::execTriggerEvent` finds matching triggers, and `MoveTrigger` changes mover state/keyframes. The MMO bridge records trigger/mover/interactive state, but full DB-driven mover materialization after load is still future work.

Step89 notes:
- Bootstrap `active_world_items` is now a DB-authoritative nearby item window, not a passive sample. The C++ server scopes it around the current HERO server position and emits `active_world_item_radius` plus a `[bootstrap_active_world_items]` diagnostic line with center/radius and source counts.
- The server builds nearby active item rows from three sources: normal `world_inventory + item_instances`, direct `world_entity_state(entity_kind='item')` rows from imported/baseline world state, and `mmo_server_world_inventory_read_model` as a compatibility fallback. Only active items with positions inside the radius are sent.
- In server-bound mode the client treats that radius as authoritative at load time: native `.sav` world items inside the window that are not present in the server active set are removed, and then server active items are spawned/updated. This makes local save world items subordinate to DB for the current proximity window.
- This is still bootstrap/load-time materialization. It is not live interest management, packet streaming, or server-side world item physics yet.



Step90 notes:
- Step89's single large CTE/UNION query for `active_world_items` can fail on MySQL syntax and must not abort the whole bootstrap snapshot. Optional nearby-world-item snapshot SQL now fails soft with a diagnostic and falls back to `[]` while the rest of the snapshot is still sent.
- Nearby active world items are queried as three isolated source arrays (`world_inventory`, `world_entity_state`, `read_model`) and concatenated in C++ instead of one CTE union. This keeps DB-authoritative nearby item materialization debuggable and avoids one source breaking the whole section.
- The server diagnostic line now reports per-source JSON byte sizes plus source counts. If the snapshot is sent but `active_world_items=0`, use `[bootstrap_active_world_items]` and any `[bootstrap_active_world_*_failed]` line to distinguish SQL failure from an empty DB/import coverage window.



Step91 notes:
- Normal accepted `movement_proposal` packets are now log-coalesced on the C++ UDP server. Do not expect one `accepted=... last=movement_proposal` line per movement packet; use `[movement_summary]` and final `summary` for movement volume.
- Movement authority remains proposal/checkpoint based: the client proposes bounded deltas, the C++ server validates distance/speed/fall limits, and accepted proposals write the current character checkpoint through `mmo_checkpoint_character_state`. Rejected movement still needs future client rollback/correction.
- The server can now queue a live world snapshot refresh after accepted movement when HERO crosses the nearby-item interest threshold. This reuses `mmo_bootstrap_snapshot_v1` chunks temporarily, but the client applies only the world/materialization slice for unsolicited snapshot ids after load.
- Live nearby item refresh is a bridge toward interest management. It is not yet typed binary item delta streaming and does not imply server-side item physics or NPC AI replication.



Step92 notes:
- Bootstrap/live snapshots now also carry a nearby NPC interest window: `nearby_npcs`, `nearby_npc_known_dialogs`, `nearby_waypoints`, `nearby_npc_radius` and `nearby_waypoint_radius`.
- The C++ server builds these nearby sections around the current HERO server position from `world_entity_state`, `content_entity_templates`, `character_known_dialogs` and `mmo_server_waypoint_read_model`. They are diagnostic/materialization input only for now; the client logs and parses counts but does not spawn, move or AI-drive NPCs from them.
- Nearby NPC/dialog/waypoint SQL is optional and must fail soft like nearby item SQL. A failure in this window must not abort character/inventory/story/bootstrap snapshot delivery.
- `tools/apply_mmo_step92_identity_admin_views.py` installs human-readable admin views that expose `BIN_TO_UUID(...,1)` UUID text and joined template keys for DB inspection. The canonical DB continues to store UUID PK/FK columns as `BINARY(16)` for compact indexed identity.




Step94 notes:
- Server-bound native save now emits a dedicated `save_checkpoint_manifest` semantic action after the normal character checkpoint. This is gated by `-mmo-client-server`; old single-player save flow remains unchanged.
- `mmo_create_save_checkpoint_manifest` writes a compact durable manifest with the latest checkpoint tick, recent journal seq and current projection row counts for inventory, equipment, quests, known dialogs, script state, world items, world inventory, interactives, NPC lifecycle and movers.
- `mmo_update_character_quest` is replaced by a UTF-8/idempotent version that journals `event_class='quest'` for new events, but tolerates replay of older `event_class='character'` quest events for existing idempotency keys.
- Bootstrap/live snapshots now include `mover_state` and `server_checkpoint_manifest`. The client currently parses and logs those sections; mover application to `MoveTrigger` remains a separate safe materialization step.
- Clean MySQL rebuilds should install Step93 and Step94 by default through `run_mmo_step55_clean_mysql_from_pre_xardas.py` / `reset_mmo_mysql_from_chapter1_start.py`.


Step95 notes:
- Step95 extends the Step94 save/checkpoint manifest into a DB-backed save-slot catalog. `mmo_save_checkpoint_manifests` now carries `save_slot_key`, `native_save_path`, `display_name`, `client_world_name` and `native_save_present`, and `v_mmo_latest_save_checkpoint_manifests` exposes the latest catalog rows per character/slot for inspection and future menu work.
- The client emits `save_checkpoint_manifest` from `GameSession::recordMmoSaveSlot`, after the native save has completed, so the server receives both the native slot path/key and the user-facing display name. The old single-player save path is still unchanged unless `-mmo-client-server` is active.
- A guarded dev bridge `-mmo-db-continue-without-native-save` / `-mmo-db-continue` lets server-bound clients bootstrap a baseline ZEN world and apply the server snapshot when the requested native `.sav` is missing. This is not final production DB-only Continue yet, but it removes the hard local `.sav` requirement for controlled tests.
- Clean MySQL rebuilds should install Step93, Step94 and Step95 by default through `run_mmo_step55_clean_mysql_from_pre_xardas.py` / `reset_mmo_mysql_from_chapter1_start.py`.


Step96 notes:
- Step95 catalog metadata is not the goal; it is only the small amount of slot/native-save context needed to label and diagnose checkpoints. The real save migration now starts at Step96.
- Server-bound native save now calls `mmo_create_db_save_checkpoint_v1`, which first writes/updates the save checkpoint manifest and then materializes normalized DB snapshot tables from current projections.
- Step96 snapshot domains are structured tables, not one opaque `.sav` blob: character position/stats, inventory, equipment, quests, known dialogs, script state, world entity state, world inventory and mover state.
- Clean MySQL rebuilds should install Step95 and Step96 by default. If reset fails in Step95, use the corrected Step95 SQL from this patch; the earlier dynamic PREPARE based column-add block was replaced.
- This is still save-time materialization, not a final restore pipeline. Next work is loading from these snapshot tables, then removing reliance on native `.sav` for server-bound Continue.

Step97 notes:
- Step97 makes Step96 snapshots part of the actual restore path. When a latest DB-native save checkpoint exists for the active session/character/world, the C++ UDP server exports it as the normal `mmo_bootstrap_snapshot_v1` payload through `mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1`; otherwise it falls back to the current live projection bootstrap.
- The exported snapshot keeps the existing client contract (`character`, `inventory`, `equipment`, `quests`, `known_dialogs`, `script_state`, `active_world_items`, `interactive_state`, `npc_lifecycle_state`, `mover_state`, `server_checkpoint_manifest`) so old server-bound apply code is reused.
- Step97 adds `mmo_save_checkpoint_world_clock_snapshot` and re-wraps `mmo_create_db_save_checkpoint_v1` so new DB save checkpoints also capture server-owned world clock state.
- This is still gated by `-mmo-client-server`; old native `.sav` save/load behavior remains unchanged without server-bound mode.
- The next hard target is strict DB-continue validation: prove that after a save, restart, and bootstrap from DB checkpoint snapshot, bookstand/script/dialog/item changes survive without depending on local `.sav` as authority.




Step98 notes:
- Bootstrap/live snapshot source is now explicit. DB-save-checkpoint restores carry `snapshot_source=db_save_checkpoint_v1`; live projection snapshots carry `snapshot_source=current_projections_v1`.
- Client strict guard `-mmo-require-db-save-checkpoint-restore` / `-mmo-strict-db-continue` rejects a server-bound restore if the downloaded snapshot is not from the latest DB save checkpoint.
- C++ server strict guard `--require-db-save-checkpoint-restore` NACKs bootstrap when no DB save checkpoint snapshot can be exported, instead of silently falling back to live projections.
- Live movement-triggered world refreshes intentionally bypass DB save checkpoint export and use current projections only. Save checkpoint restore is for boot/continue; live refresh must not replay an old save snapshot over active movement/item interest updates.
- `mmo_validate_latest_save_checkpoint_restore_v1`, `mmo_assert_latest_save_checkpoint_restore_v1` and `v_mmo_latest_save_checkpoint_strict_restore` are the DB-side evidence surface for proving DB-native Continue readiness.
