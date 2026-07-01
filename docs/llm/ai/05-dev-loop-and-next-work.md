# 05 Dev Loop And Next Work

Clean DB rebuild for a fresh game/server test:

```bash
cd ~/Desktop/OpenGothic

python3 tools/run_mmo_step55_clean_mysql_from_pre_xardas.py \
  --sqlite runtime/g2notr_ch1_pre_xardas.sqlite \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --i-understand-this-drops-database
```

If the C++ UDP server is already running, wait for the clean rebuild command to
finish before starting the client. The server should recover its stale cached DB
session automatically on the next bootstrap and print `[db_session_recovered]`.

Build C++ ASIO UDP server:

```bash
cd ~/Desktop/OpenGothic
cmake -S server/cpp -B build/mmo_cpp_server -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/mmo_cpp_server --target mmo_udp_server -j
```

Terminal 1, C++ UDP server:

```bash
cd ~/Desktop/OpenGothic

./build/mmo_cpp_server/mmo_udp_server \
  --bind 127.0.0.1:29777 \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO
```

Normal server output should include:

```text
db_session=... direct_db=on enqueue_outbox=off
```

After a repeated clean DB rebuild without restarting the server, output may also
include:

```text
[db_session_recovered] reason=bootstrap old=... new=...
```

Terminal 2, client. This single flag selects server-bound transport and load-time
materialization from the server bootstrap snapshot:

```bash
cd ~/Desktop/OpenGothic

./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-client-server 127.0.0.1:29777 \
  -mmo-action-session-key local-dev-PC_HERO_TEST
```

Do not start `run_mmo_live_resolved_worker.py` in the normal loop. The client
currently warns if `-mmo-restore-snapshot-json runtime/pc_hero_test_live/mysql_restore_snapshot.json`
points at a missing file; omit that flag in the normal loop. The C++ server now
produces `runtime/mmo_server_bootstrap_snapshot.json` after bootstrap.

Expected first evidence:
- If the DB was just rebuilt while the server stayed open, C++ server prints
  `[db_session_recovered] ...`.
- C++ server prints `bootstrap_ack accepted=1 ready=1 ...`.
- C++ server prints `bootstrap_snapshot_sent id=... bytes=... chunks=...`.
  If it prints `[bootstrap_snapshot_build_failed]`, treat it as a server SQL
  bug even when `bootstrap_ack ready=1`.
- Client logs `MMO server bootstrap snapshot receiving`, progress, and then
  `MMO server bootstrap snapshot received`. It writes
  `runtime/mmo_server_bootstrap_snapshot.json` plus
  `runtime/mmo_server_bootstrap_snapshot_manifest.json`.
- Client logs periodic `MMO server ACK summary ...`; accepted generic ACKs are
  summarized rather than printed one-by-one. It still logs bootstrap ACKs,
  rejected ACKs and `MMO server diagnostic ... reason=...`.
- Client logs `MMO server snapshot world state applied: ...` with removed world
  items, applied interactives and applied NPC lifecycle when `-mmo-client-server`
  materializes DB world state at load.
- C++ server prints `direct_db=` increasing for movement, checkpoint,
  dialog/script/quest/progression/interactive/container-take/pickup/drop/equip/weapon actions.
- C++ server keeps `enqueued=0` unless explicitly started with
  `--enqueue-outbox`.
- Invalid movement produces `[movement_rejected]`, a movement NACK and a
  `ServerDiagnostic`. Tiny stale movement deltas after weapon draw/holster should
  be accepted; only spatially impossible proposals should be rejected.
- Pickup failures include `target=` and `payload=` in `[direct_db_failed]`;
  resolver should map hook `world-item:...:pid:...:sym:...`, malformed
  `world-item.zen:pid:...:sym:...` and DB `world_item:...` rows.

Optional fallback/debug only:

```bash
./build/mmo_cpp_server/mmo_udp_server \
  --bind 127.0.0.1:29777 \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --session-key local-dev-PC_HERO_TEST \
  --character-key PC_HERO \
  --enqueue-outbox
```

Next work:
1. Verify binary bootstrap packet -> C++ direct `bootstrap_ack` plus
   `runtime/mmo_server_bootstrap_snapshot.json` and the snapshot manifest.
2. Verify direct C++ `character_checkpoint` writes update
   `character_positions` and `character_checkpoint_audit`.
3. Verify direct C++ movement/dialog/script/quest/container-take/pickup/drop/equip writes
   against the same DB checks previously proven by the worker.
4. Add typed movement packet payload; stop using generic JSON payload for
   movement hot path.
5. Move remaining trade/spell/resource edge domains into C++ direct handlers.

Current load answer:
- Without `-mmo-client-server`, loading still uses the native save/new-game
  path only.
- With `-mmo-client-server`, the client downloads a DB snapshot after
  bootstrap and stores it under `runtime/mmo_server_bootstrap_snapshot.json`.
- That downloaded snapshot is applied to HERO stats/resources, inventory/equipment,
  position, quest log, known dialogs and safe full character integer script state in server-bound mode.

Daily discipline:
- Use `PC_HERO_TEST` / `local-dev-PC_HERO_TEST`.
- Keep old numbered step sessions as history unless explicitly debugging them.
- If runtime artifacts are wiped, preserve `runtime/g2notr_ch1_pre_xardas.sqlite`.
- Keep old single-player behavior unchanged without MMO flags.




Step78 normal run expectations:

```text
MMO server snapshot restore scheduled: server_bound=1 inventory=1 position=1 stats=1 story=1 ...
MMO server bootstrap snapshot received: bytes=... chunks=...
MMO server snapshot stats applied: level=... exp=... lp=... hp=... mana=...
MMO server snapshot inventory applied: inventory=... equipment=... restore_items=...
MMO server snapshot position applied: x=... y=... z=...
MMO server snapshot story applied: mode=replace_from_server quests=... restore_quests=... known_dialogs=... restore_known_dialogs=... script_ints=... restore_script_ints=...
```

Do not use `-mmo-server-snapshot-apply-inventory` or
`-mmo-server-snapshot-apply-position` in normal commands. They are compatibility
no-ops now; `-mmo-client-server` is the server-bound materialization switch.

Next work:
1. Verify no restore-time `equip_character_item`/movement echo is emitted after
   applying the server snapshot.
2. Verify `take_container_item` has `rejected=0` when looting chests and that
   `world_inventory` rows disappear or decrement.
3. Add typed movement packet payload and stop using JSON for the movement hot
   path.
4. Move remaining trade/spell edge cases into direct C++ handlers.






Step80 normal run expectations:

```text
MMO server snapshot restore scheduled: server_bound=1 inventory=1 position=1 stats=1 story=1 reason=new_game_pre_start_loaded ...
MMO server bootstrap snapshot received: bytes=... chunks=...
MMO server snapshot story applied: mode=replace_from_server ... restore_script_ints=...
```

If the player executes a dialog before a delayed snapshot is applied, the expected safe fallback is:

```text
MMO server snapshot script state skipped: local story changed before snapshot arrived ...
MMO server snapshot story applied: mode=merge_preserve_local ...
```

Next work after Step80:
1. Verify Xardas no longer restarts the same non-permanent dialog after the player ends it.
2. Start applying selected world current projections: tombstoned/taken world items first, then interactives, then NPC positions.
3. Continue splitting `server/cpp/mmo_udp_server.cpp` into small snapshot, MySQL/session, resolver and direct-domain modules.




Next Step81 work outcome:
- Verify after picking a world item and using/changing an interactive that the second client launch reports non-zero `world_item_deltas` or `interactive_state` and keeps the local world consistent with DB truth.
- Next large slice after this is NPC lifecycle/position materialization. Keep AI/path queues out of DB-backed restore.




Step82 normal run expectations:

```text
MMO server bootstrap snapshot received: bytes=... chunks=...
MMO server snapshot world state applied: world_item_deltas=... removed_world_items=... already_absent_world_items=... active_world_items=... spawned_world_items=... updated_world_items=... interactive_state=...
```

After Step82, snapshot size should drop versus Step81 because full interactive `state_json` and duplicated legacy world samples are no longer emitted. Test by picking up one world item, dropping one item, exiting, and launching again: the picked item should stay gone, while the dropped DB-owned item should be spawned or updated from `active_world_items`.

Next large slice after Step82:
1. Add NPC lifecycle/position materialization for non-AI static state only.
2. Keep routine/path/fight queues local/transient until server AI exists.
3. Add typed binary payloads for hot movement and inventory actions.



Step83 normal run expectations:

```text
accepted=... direct_db=... failed=0 last=apply_world_entity_damage
accepted=... direct_db=... failed=0 last=mark_npc_dead
accepted=... direct_db=... failed=0 last=loot_npc_inventory
MMO server ACK summary accepted=... rejected=0 ... diagnostics=0
```

For an already-created clean DB that predates Step83, install the combat bridge once before testing sheep/NPC kills:

Step87 normal run expectations:

```text
[observed_npc_loot_materialized] owner=npc:newworld.zen:pid:...:sym:... symbol=... amount=... item=...
accepted=... direct_db=... failed=0 last=loot_npc_inventory
MMO server ACK summary accepted=... rejected=0 ... diagnostics=0
```

`[npc_loot_grant_fallback]` should now be rare for corpse loot. If it appears,
the log includes both `resolve_reason=` and `materialize_reason=`; treat that as
the next concrete DB/template/source identity issue to fix. A normal save/load
after kills should continue to show:

```text
MMO server snapshot world state applied: ... npc_lifecycle_state=... applied_npc_lifecycle=...
```

The server still does not send live NPC movement/AI updates. Observed NPC and
observed corpse-loot materialization are load-time/server-state bridges, not a
server simulation loop.

Step88 normal run expectations:

```text
[observed_npc_loot_materialized] owner=npc:newworld.zen:pid:...:sym:... symbol=... amount=... item=...
[observed_world_item_materialized] entity=world-item:newworld.zen:pid:...:sym:... symbol=... amount=...
accepted=... direct_db=... failed=0 last=pickup_world_item
accepted=... direct_db=... failed=0 last=loot_npc_inventory
```

The old warning below should become rare and include both reasons if it still
appears:

```text
[npc_loot_grant_fallback] ... resolve_reason=... materialize_reason=...
[world_item_pickup_grant_fallback] ... resolve_reason=... materialize_reason=...
```

If MySQL prints `world_event_journal_event_class_ck`, check that new server
events use one of the allowed classes: `character`, `inventory`, `equipment`,
`world_entity`, `quest`, `dialog`, `script`, `combat`, `trade`, `spell`,
`system`, `diagnostic`.

```bash
python3 tools/apply_mmo_step83_combat_lifecycle_bridge.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --output runtime/step83_combat_lifecycle_bridge/apply.json
```

Future destructive clean rebuilds install Step83 by default through `run_mmo_step55_clean_mysql_from_pre_xardas.py`.

Next large slice after Step83:
1. Verify a second launch after killing/looting a creature keeps the NPC dead and does not recreate its inventory.
2. Add load-time NPC lifecycle materialization for dead/removed NPCs without moving active routine NPCs yet.
3. Add server-side corpse/container policy instead of treating dead NPC inventory as a generic container forever.





Step84 normal run expectations:

```text
accepted=... direct_db=... failed=0 last=apply_world_entity_damage
accepted=... direct_db=... failed=0 last=mark_npc_dead
accepted=... direct_db=... failed=0 last=pickup_world_item
MMO server snapshot world state applied: ... npc_lifecycle_state=... applied_npc_lifecycle=...
```

For an already-created DB that predates Step84, install the bridge once before testing goblin/sheep kills and corpse/world-item pickup:

```bash
python3 tools/apply_mmo_step84_world_identity_lifecycle_bridge.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --output runtime/step84_world_identity_lifecycle_bridge/apply.json
```

Future destructive clean rebuilds install Step84 by default through the clean DB tools.

Next large slice after Step84:
1. Add server memory read-model materialization for active NPC spawn/lifecycle state at shard start.
2. Add a server-side NPC simulation tick for routine/passive movement before attempting live NPC movement replication.
3. Replace the fallback unresolved-pickup grant with authoritative server-owned corpse/drop inventory spawning.


Step85 normal run expectations:

```text
accepted=... direct_db=... failed=0 last=apply_world_entity_damage
accepted=... direct_db=... failed=0 last=mark_npc_dead
accepted=... direct_db=... failed=0 last=loot_npc_inventory
```

If a corpse item is local-only and not yet in `world_inventory`, the expected dev bridge log is:

```text
[npc_loot_grant_fallback] target=... symbol=... amount=... reason=...
accepted=... direct_db=... failed=0 last=loot_npc_inventory
```

No new SQL bridge is required for Step85 if Step84 is already installed. Rebuild only the C++ server.

Next large slice after Step85:
1. Materialize active NPC read models in server memory at server start.
2. Add a minimal server-side NPC lifecycle/runtime table for dead/disabled/respawn state before any movement AI.
3. Design the first server tick for passive routine state, then add typed NPC position replication packets.


Step86 normal run expectations:

```text
[observed_world_npc_resolve_fallback] action=apply_world_entity_damage target=npc:...
[observed_world_npc_materialized] entity=npc:newworld.zen:pid:...:sym:...
accepted=... direct_db=... failed=0 last=apply_world_entity_damage
accepted=... direct_db=... failed=0 last=mark_npc_dead
```

For corpse/local drops, this may still appear and is acceptable until server-owned corpse inventory exists:

```text
[npc_loot_grant_fallback] target=... symbol=... amount=... reason=...
accepted=... direct_db=... failed=0 last=loot_npc_inventory
```

No SQL installer is required for Step86 if Step84/Step85 DB bridge procedures are already present.

Next large slice after Step86:
1. Use observed runtime NPC rows in `npc_lifecycle_state` on the next bootstrap and verify killed goblins/wolves stay dead after reload.
2. Replace local corpse meat grants with server-owned corpse/drop inventory rows.
3. Start server memory read-model materialization for active NPCs before any live AI/position replication.

Step89 normal run expectations:

```text
[bootstrap_active_world_items] bytes=... center=... radius=12000.000000 wes_item_total=... wes_item_near=... world_inventory_total=... read_model_near=...
MMO server snapshot world state applied: ... active_world_items=... spawned_world_items=... updated_world_items=... authoritative_window=1 ... removed_local_items_not_in_db=...
```

If `world_inventory_total` is non-zero but `wes_item_near=0`, `read_model_near=0` and `active_world_items=0`, the bootstrap query is proving a DB/import coverage issue: the current DB has no active item rows with positions near HERO. Do not debug this as UDP/client snapshot loss.

Next large slice after Step89:
1. Add live interest-window refresh packets for active world items instead of only bootstrap-time materialization.
2. Add per-item ack/correction for pickup/drop so client UI cannot race against server ownership.
3. Move world-item projection SQL out of `mmo_udp_server.cpp` into a focused snapshot/read-model module.



Step90 normal run expectations:

```text
[bootstrap_active_world_items] bytes=... world_inventory_bytes=... world_entity_bytes=... read_model_bytes=... center=... radius=12000.000000 wes_item_total=... wes_item_near=... world_inventory_total=... world_inventory_item_near=... read_model_near=...
bootstrap_snapshot_sent id=... bytes=... chunks=...
```

If one optional item source fails, the server should still send the bootstrap snapshot and print a focused diagnostic such as:

```text
[bootstrap_active_world_entity_items_failed] error=...
```

Do not regress to `[bootstrap_snapshot_build_failed]` for optional nearby item window problems. That failure should be reserved for required character/session/story/world snapshot sections.



Step91 normal run expectations:

```text
[movement_summary] accepted=... received=... movement_lines_suppressed=... direct_db=... failed=0
[live_world_item_snapshot_queued] reason=movement_interest x=... y=... z=... bytes=...
bootstrap_snapshot_sent id=... bytes=... chunks=...
MMO server live world snapshot applied: snapshot_id=... active_world_items=... updated_world_items=... authoritative_window=1 ...
```

The old noisy line below should no longer appear for every accepted movement proposal:

```text
accepted=... failed=0 last=movement_proposal
```

Next large slice after Step91:
1. Replace reused full snapshot refresh with a typed binary `WorldItemWindowDelta` packet.
2. Add client-side correction/rollback for rejected movement and rejected pickup/drop actions.
3. Start extracting snapshot/read-model SQL from `mmo_udp_server.cpp` into focused modules.



Step92 normal run expectations:

```text
[bootstrap_nearby_npcs] bytes=... known_dialog_bytes=... waypoint_bytes=... center=... radius=12000.000000 npc_total=... npc_near=... waypoint_near=...
MMO server snapshot world state applied: ... nearby_npcs=... parsed_nearby_npcs=... nearby_npc_known_dialogs=... nearby_waypoints=... recent_actions=...
MMO server live world snapshot applied: snapshot_id=... active_world_items=... nearby_npcs=... nearby_waypoints=... recent_actions=...
```

Install readable admin identity views after a clean DB rebuild when inspecting BLOB UUIDs in GUI tools:

```bash
python3 tools/apply_mmo_step92_identity_admin_views.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean" \
  --output runtime/step92_identity_admin_views/apply.json
```

Useful admin views:

```sql
SELECT * FROM v_mmo_admin_item_instances_readable LIMIT 50;
SELECT * FROM v_mmo_admin_entity_templates_readable WHERE entity_kind IN ('npc','creature') LIMIT 50;
SELECT * FROM v_mmo_admin_world_entities_readable WHERE entity_kind IN ('npc','creature','item') LIMIT 50;
```

Next large slice after Step92:
1. Move item/NPC/window SQL out of `mmo_udp_server.cpp` into a focused snapshot/read-model module.
2. Replace reused full JSON snapshot refresh with typed binary interest-window deltas.
3. Start client correction for rejected movement/pickup instead of only logging ACK/NACK.


Step93 save-to-server roadmap notes:
- `docs/llm/ai/06-save-to-server-roadmap.md` is the temporary LLM roadmap for replacing native `.sav` authority with DB/server authority. Read it before designing further DB-only load/save work.
- Native `.sav` currently stores much more than HERO stats/inventory: session/world time, visited worlds, current world, camera, quests/dialogs, Daedalus globals, portal guilds, NPC arrays, invalid NPCs, world items, mobsi/interactives, trigger queues, routines, mover state and per-NPC AI/movement/fight internals.
- DB-only play should not copy raw save internals 1:1. Persist durable facts and server projections; keep camera/render/audio/particles and raw AI/fight queues out of production authority.
- Immediate next blockers for DB-only work are: UTF-8/idempotency fix for non-ASCII quest/dialog keys, save/checkpoint manifest, baseline+DB load without `.sav`, mover materialization, waypoint import/read-model and then server-side NPC routine prototype.

