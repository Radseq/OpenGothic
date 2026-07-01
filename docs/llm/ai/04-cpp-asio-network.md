# 04 C++ ASIO Network

Current network step:
- Binary UDP packet v1 is shared in `game/game/mmonetprotocol.h`.
- Client sends packets from `mmosemanticactionsink.cpp` only when
  `-mmo-client-server` is enabled.
- C++ server executable: `server/cpp/mmo_udp_server.cpp`.
- Transport no longer depends on JSONL files.

Packet v1:
- magic/version/kind;
- semantic action kind id;
- packet/local sequence and client tick;
- session key;
- target key;
- idempotency key;
- payload bytes.

Server packet types:
- `ServerAck` confirms/rejects a semantic action and marks bootstrap readiness.
- `ServerSnapshotChunk` carries chunked JSON for the first DB-backed bootstrap
  snapshot. Chunks are reassembled by `mmosemanticactionsink.cpp`.
- `ServerDiagnostic` carries server-side reject/failure details after ACK/NACK
  so the client can log `action`, `reason` and `message`.

Important compromise:
- The UDP frame is binary and ASIO-based now.
- The semantic payload is still the existing hook payload string internally,
  because all gameplay hooks already emit it and the current MySQL procedures
  can accept bridge metadata JSON.
- Next protocol iteration should replace hot payloads for movement/inventory
  with typed binary structs, one domain at a time.

Server behavior now:
- listen on UDP `127.0.0.1:29777` by default;
- decode binary client action packet;
- de-duplicate idempotency keys in process memory;
- optionally call `mmo_login_character`;
- validate the cached DB session before bootstrap/direct DB calls and relogin
  when local clean DB rebuild invalidated the old session UUID;
- allow a repeated bootstrap packet to restart the in-memory dedupe set for the
  local dev loop;
- handle `client_bootstrap_request` directly by checking typed MySQL read-model
  tables and returning a binary bootstrap ACK;
- handle `character_checkpoint` directly by calling
  `mmo_checkpoint_character_state` and returning a binary ACK/NACK;
- validate/apply `movement_proposal` directly and NACK invalid movement;
- call direct MySQL procedures for dialog/script-int/quest/progression,
  resource delta/mana, trigger/mover, interactive use/state,
  pickup/remove/drop/transfer/loot item, equip/unequip and weapon state;
- resolve live `pickup_world_item` keys from hook form
  `world-item:<world>:pid:<pid>:sym:<symbol>` to DB form
  `world_item:<world>:<pid>:<symbol>:...`;
- use live-table bootstrap readiness fallback when physical Step53 read models
  are empty but the clean MySQL import has `world_entity_state` rows;
- after a ready bootstrap, build `mmo_bootstrap_snapshot_v1` from live MySQL
  tables and send it as chunked UDP snapshot packets;
- enqueue to `mmo_server_action_outbox` only when `--enqueue-outbox` is passed;
- send binary ACK with accepted/rejected flag;
- send binary diagnostics for bootstrap snapshot errors, direct DB failures,
  movement rejects and unhandled direct actions.

Client behavior now:
- the server-bound UDP sink receives `ServerAck` packets and logs bootstrap or
  rejected action ACK/NACK state;
- the sink receives `ServerDiagnostic` packets and logs server-side reasons for
  failures/rejections;
- the sink receives `ServerSnapshotChunk` packets, reassembles them and writes
  `runtime/mmo_server_bootstrap_snapshot.json`;
- GameSession polls that downloaded snapshot and, when `-mmo-client-server` is
  active, applies HERO stats/resources, inventory/equipment, position, quest log,
  known dialogs and safe full character integer script state from DB truth;
- world deltas remain downloaded-only; script state is apply-on-load only when no local story action raced ahead of the snapshot.

Step75 SQL/runtime notes:
- Bootstrap snapshot SQL must not reference `realm_world_instances.world_name`;
  the current schema provides `realm_world_instances.world_instance_key` and
  `content_world_templates.world_name`.
- Snapshot v1 now includes character, inventory, equipment, dialogs, quests,
  script sample, world clock, world inventory sample, interactive sample, world
  delta sample and recent event sample.
- Movement validation allows long idle/weapon-state gaps for tiny deltas while
  preserving coordinate, step, horizontal speed and vertical/fall constraints.

Why outbox remains:
- It is useful for diffing old resolver behavior while C++ direct handlers are
  still young.
- It is not the normal loop and does not require starting a Python worker.
- Unknown direct actions should be visible as `[direct_db_unhandled]`; add a
  focused C++ handler instead of adding another tool script.

Near-term C++ server migration:
1. Verify C++ direct `client_bootstrap_request -> bootstrap_ack` and
   `bootstrap_snapshot_sent` against clean MySQL live tables.
2. Keep C++ direct `character_checkpoint -> mmo_checkpoint_character_state`
   green and observable via `direct_db=`.
3. Harden the direct C++ movement validator and make the client consume movement
   ACK/NACK.
4. Replace JSON bridge payloads with typed movement/inventory/dialog packets.
5. Move remaining non-covered domains such as container/trade/spell edge cases
   into direct C++ handlers.
6. Extend server-produced bootstrap/restore materialization beyond the current
   HERO stats/resources, inventory/equipment and position slice.

Do not do yet:
- do not add a complex packet framework before one end-to-end slice is green;
- do not move DB calls into the OpenGothic game thread;
- do not make the server reset world state from baseline on login;
- do not delete the clean MySQL rebuild path.
- do not assume the C++ server process must be restarted after every clean DB
  rebuild; it should relogin when it detects a stale session.

Build note:
- Root `CMakeLists.txt` must add `thirdparty/asio/include` as a `SYSTEM`
  include and define `ASIO_STANDALONE`; otherwise OpenGothic's
  `-Wconversion -Werror` can fail inside ASIO headers.

Step76 client receive/observability notes:
- The client sink uses a timed wait while idle so it continues receiving server
  packets even when no new semantic actions are being sent.
- The UDP socket receive buffer is enlarged for local snapshot bursts.
- Bootstrap snapshot receive now clears stale snapshot artifacts before sending
  a fresh bootstrap request, writes the JSON through a temporary file and emits
  a small manifest beside it.
- The client logs snapshot receive/progress, periodic ACK summaries and final
  ACK summary on sink shutdown. Do not expect one console line per accepted
  gameplay ACK.
- The server paces bootstrap snapshot chunk bursts slightly to reduce local UDP
  receive-buffer loss during large first snapshots.



Step78 client materialization:
- `-mmo-client-server` is the single global server-bound materialization switch.
  Do not add separate apply flags for each restored snapshot domain.
- GameSession waits for the downloaded `mmo_bootstrap_snapshot_v1` file and
  applies HERO stats/resources, inventory/equipment and position.
- Restore-time local mutations are wrapped in semantic capture suppression so
  applying server truth does not generate duplicate equip/movement intents.
- This is still not real-time replication; it is load-time restore from server
  truth after bootstrap.






Step79 network/runtime notes:
- `take_container_item` is now the owner-aware server-bound path for taking items from chests/containers. It carries `source_entity_key`, `source_container_key` and `container_key`; the server resolves the item from `world_inventory(owner_entity_key, symbol)` and calls the world-inventory loot bridge.
- `transfer_character_item` must not be emitted from generic `Inventory::transfer` unless a future hook includes both stable source and target character identities. The server treats identity-poor legacy transfer packets as no-op accepted compatibility packets.
- Bootstrap snapshot apply now includes quest log and known dialogs in addition to HERO stats/inventory/equipment/position. Script ints remain downloaded-only until the server sends a complete typed script-state section.




Step80 network/runtime notes:
- New Game server-bound bootstrap is requested before `triggerOnStart(true)` and the load path waits briefly for the UDP sink to assemble the snapshot. This avoids applying pre-start DB truth after the first Xardas dialog already ran.
- Snapshot v1 now emits `script_state` with full bounded character int/array_int rows. `script_state_sample` remains parser fallback only.
- If a dialog is executed before snapshot apply, the client uses story merge mode and skips script-int restore for that stale snapshot.




Step81 protocol note:
- `mmo_bootstrap_snapshot_v1` now carries `world_item_deltas` for non-active world item tombstones, `active_world_items` for active server-owned world items and `interactive_state` for mobsi/interactives. Legacy sample aliases may be empty compatibility fields and must not be treated as authority.
- These sections are still JSON inside the bootstrap snapshot; hot runtime protocol should later move world item and interactive deltas to typed binary packets.




Step82 protocol note:
- Snapshot world sections are still JSON, but they are now slim projection rows. Avoid reintroducing raw `state_json` for large arrays; add typed fields that the client actually applies.
- `active_world_items` is bounded separately from container/world inventory counts so bootstrap size remains stable as container content grows.




Step84 network note:
- The UDP server still does not send live NPC/mob position streams. It sends bootstrap snapshot chunks, ACK/NACK and diagnostics. NPC lifecycle is materialized at load through `npc_lifecycle_state`; real-time NPC movement/AI replication needs a future server-side world simulation tick.
- Direct combat handlers now use a hardened identity resolver that accepts canonical, DB and malformed legacy NPC/world-item keys and can fall back to target position for combat hit resolution.

Step85 network note:
- Combat resolver fallback now also uses `content_entity_templates` and `creature:` aliases, then a bounded same-symbol nearest-position search. This is for local save/load pid drift, not for live NPC replication.
- Corpse `loot_npc_inventory` can fall back to a durable symbol grant when the dead NPC or generated corpse item is not yet represented as authoritative server-owned inventory. Chest/container loot must still resolve through `world_inventory`.

Step86 network note:
- If `apply_world_entity_damage` or `mark_npc_dead` cannot resolve the target NPC, the C++ server can insert a minimal observed runtime world entity and then retry the normal direct DB combat path. The insert is journaled as `world_npc_observed` using an idempotency suffix before the combat event.
- This reduces NACKs for locally spawned/save-only creatures, but does not introduce server-driven NPC movement packets or AI state replication.



Step91 network note:
- The UDP server suppresses per-packet success logs for accepted movement proposals and emits `[movement_summary]` periodically instead. Rejections, diagnostics, bootstrap, item/combat/story actions and live snapshot sends still log explicit lines.
- Movement proposals are still handled directly in C++ DB mode by validating transform deltas and checkpointing on accept. This is conceptually the old movement gate moved from Python/debug receiver into the direct ASIO/MySQL path.
- When accepted movement crosses `LiveWorldItemRefreshDistance` or a bounded time+distance threshold, the server sends a refreshed snapshot id. The client snapshot sink writes `snapshot_id` to the manifest, and GameSession applies only world state for newer unsolicited snapshot ids after the initial full bootstrap restore.



Step92 network note:
- Snapshot interest management now has a second read-only window beside items: nearby NPCs, known dialog facts for those NPCs when resolvable, and nearby waypoints. This is not live NPC replication. It is a server-side visibility/read-model slice so future AI/runtime work can stop relying on full-world dumps.
- Do not drive local NPC movement from `nearby_npcs`. Active NPC pathing, routine execution, perception and combat decisions remain local-client/native until a dedicated server simulation tick exists.
