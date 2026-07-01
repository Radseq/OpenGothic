# 05 Server-First Roadmap

The database layer is done enough. Do not add more DB tables unless a real server/hook/replay need exposes a gap. Next work is server-first.

Step 31 - Compact AI context
- Replace large `docs/llm/ai` set with this compact set.
- Keep old docs only in `docs/llm/legacy/ai-precompact-*` for archaeology.
- Acceptance: future AI reads <=7 compact files and still knows DB status, identities, hook sites, transient exclusions and roadmap.

Step 32 - Semantic action contract in C++
- Add compact C++23 types for action kind, entity key, idempotency key, payload metadata.
- Keep constexpr registries for action names and DB procedure/event mapping.
- Add `MmoSemanticActionSink` no-op default and optional bounded queue. No MySQL calls on game thread.
- Acceptance: compiles disabled by default; zero behavior change; can unit/smoke serialize a sample action.

Step 33 - Minimal MMO server boundary
- Create a small server process/module boundary: receive semantic action envelope, authenticate/dev-session map, validate basic shape, then later dispatch to MySQL procedure or reject.
- First implemented transport may be local UDP JSONL receiver for dev evidence. This is not final reliable networking; it proves client -> server-boundary without client -> MySQL.
- Acceptance v1: pickup/equip actions reach a separate receiver process and validate/de-duplicate by idempotency key.
- Acceptance v2: one action kind roundtrips through server to DB with idempotent retry.


Optional server bootstrap note:
- A future MMO server may load NPC equipment, inventory, stats, lifecycle and position from content baseline/current-state projections into an in-memory runtime model during world startup.
- Treat that as bootstrap/state materialization, not as thousands of live `equip_character_item` actions.
- Live semantic actions should describe accepted gameplay changes after startup; bootstrap loading may have separate diagnostics/import evidence if needed.
- This is guidance, not a hard constraint: verify against the eventual server architecture and parity results before making it authoritative.

Step 34 - Hook first vertical slices
- Add post-success hooks for `World::takeItem/removeItem`, `Inventory::transfer`, `Inventory::equip/unequip`.
- Submit to sink, not DB. Include actor, item, source/target, amount, slot, stable world/entity keys.
- Acceptance: playing pickup/equip produces action envelopes; no blocking; disabled path has no measurable gameplay impact.

Step 35 - Server dispatch for inventory/world-item slice
- Server validates action shape, world/session/actor/target, calls existing `mmo_*` procedures or enqueues `mmo_server_action_outbox` as needed.
- Acceptance: DB journal has semantic events; repeated action idempotency does not duplicate item/stack/gold.

Step 36 - Parity runner v1
- Automate one scenario: pickup item + equip/unequip. Compare native `.sav`, SQLite save-slot and MySQL projection hashes.
- Acceptance: scenario row becomes passed only from real artifacts, not manual override.

Step 37 - Script/progression vertical slice
- Add hooks/evidence for bookstand/regal: script global/known dialog/quest/XP/LP deltas around `GameScript::exec` and progression procedures.
- Acceptance: `bookstand_script_xp` parity scenario can pass, proving one-shot script flag + XP reward cannot be duplicated.

Step 38 - NPC/trade/combat slices
- Trade: `Npc::buyItem/sellItem` through server, no double gold spend.
- Combat: ammunition/mana consumption, damage, `Npc::onNoHealth` lifecycle event.
- NPC state: add hot-path read projection only if server validation needs it.
- Acceptance: trade, NPC killed and combat/resource parity scenarios pass.

Step 39 - Movement/server authority foundation
- Client sends input/intent or movement proposals. Server validates speed/collision/world bounds using equivalent rules/content.
- Add interest management and replication snapshots. DB stores checkpoints, not every frame.
- Acceptance: reconnect loads last checkpoint; clients receive nearby entity deltas; DB write rate remains bounded.

Step 40 - Productionization
- Replace dev mysql-cli worker with production worker/RPC. Add metrics, backpressure, replay runner, crash recovery, shard orchestration.
- Acceptance: server can rebuild projections from content baseline + `world_event_journal`; full required parity suite green; only then consider DB-only strict load.

Do not skip parity. Required real scenarios: bookstand/script XP, world item pickup, equip/unequip, container change, quest progress, dialog consumed, NPC killed, chapter change, save-restart-load.

## Current implementation status

Step 32 is started/completed as a first C++ contract pass:
- action registry exists;
- envelope JSONL serialization exists;
- disabled-by-default sink exists;
- bounded queued JSONL dev transport exists;
- command-line controls exist.

Step 34 is started with first vertical slices:
- pickup/remove world item;
- generic inventory transfer;
- equipment equip/unequip.

Next Step 33/35 work:
- replace JSONL-only dev sink with a server transport sink;
- create minimal MMO server receiver for semantic envelopes;
- map `pickup_world_item`, `remove_world_item`, `transfer_character_item`, `equip_character_item`, `unequip_character_item` to existing MySQL procedures/outbox;
- keep idempotency server-side and reject invalid session/world/target/amount/slot.



## Step 33 UDP receiver v1 implemented

Added a dev-only UDP semantic action transport:
- Client flag `-mmo-action-udp <ipv4:port>` sends the same immutable JSONL envelope through the async semantic action worker.
- `-mmo-action-jsonl` can be used at the same time as local evidence.
- `tools/run_mmo_action_receiver.py` listens on UDP, validates shape, de-duplicates `idempotency_key`, and writes accepted raw actions to JSONL.
- This is a server-boundary smoke, not final transport and not DB dispatch.
- Game thread remains free of socket/DB I/O; socket send happens in the existing semantic action worker.

## Step 35 outbox enqueue bridge v1 implemented

Added receiver-side MySQL outbox enqueue without putting MySQL inside the OpenGothic process:
- `tools/run_mmo_action_receiver.py` can now receive UDP semantic envelopes, validate/dedupe them, write server JSONL evidence and optionally enqueue accepted actions into `mmo_server_action_outbox`.
- Receiver does dev `mmo_login_character(...)` once to obtain an active `server_sessions.session_id`; each accepted envelope is enqueued through `mmo_enqueue_server_action(...)`.
- The original client envelope is preserved in `request_payload.client_payload/client_*` fields; best-effort dispatch aliases such as `world_item_entity_key` and `equipment_slot` are included.
- Current game envelopes do not yet contain MySQL `item_instance_id` UUIDs, so many inventory/world-item actions are `dispatch_ready=false`. This is correct: the next server step needs a resolver from engine stable keys to DB rows before executing procedures.
- `tools/check_mmo_action_receiver_outbox.py` inspects receiver-enqueued outbox rows and dispatch contract gaps.
- `tools/replay_mmo_actions_to_receiver.py` replays local JSONL to the receiver for duplicate/idempotency smoke tests.

Current recommended sequence:
```text
OpenGothic hook -> async UDP -> receiver -> JSONL evidence -> mmo_server_action_outbox pending rows
```

Still not done:
```text
outbox pending row -> key resolver -> exact mmo_* procedure -> event journal/projection -> parity proof
```

## Step 35 resolved dispatch v2 implemented

Added the first real receiver/outbox -> MySQL procedure bridge without adding MySQL to the OpenGothic process:
- `tools/run_mmo_action_receiver.py` now writes better resolver hints into outbox payloads: engine world-item key, source persistent id, item symbol, normalized equipment slot, resolver/direct readiness flags.
- `tools/run_mmo_resolved_action_worker.py` claims `mmo_server_action_outbox`, resolves engine keys against MySQL projections, calls existing procedures, and marks rows applied/failed.
- Supported v2 dispatch: `pickup_world_item`, `remove_world_item`, `equip_character_item`, `unequip_character_item`.
- Resolver is conservative: exact or unique DB entity/item match is required. Ambiguous item/template matches fail instead of faking server authority.
- `tools/check_mmo_action_dispatch_results.py` checks outbox status, event journal rows and worker telemetry by session/idempotency prefix.

Current target loop:
```text
OpenGothic hook -> async UDP -> receiver -> outbox -> resolved worker -> mmo_* procedure -> world_event_journal/projection
```

Important limitation:
- This is still a dev bridge. UDP is not reliable final networking, and the resolver works only when imported MySQL keys/projections match the current OpenGothic runtime keys well enough.
- Do not mark parity scenarios passed only because this worker applied an outbox row. Parity still needs native `.sav` + SQLite save-slot + MySQL projection artifacts.


## Step 35 v2.1 resolver/worker hardening

Added resolved worker isolation and diagnostics:
- `run_mmo_resolved_action_worker.py --session-key` claims only rows for the selected dev replay prefix.
- The worker stops on first failure by default so dependent actions are not executed after a failed prerequisite.
- The world-item resolver now treats inactive matched rows as resolver/projection mismatch before calling the stored procedure.
- `inspect_mmo_action_resolution.py` gives read-only evidence for resolver candidates and projection mismatches.

Current blocker found by real test:
- The receiver/outbox path works.
- The MySQL projection used for the test did not have the picked world items active, so pickup dispatch failed correctly.
- Next valid dispatch test needs a client action set produced against a matching DB projection or a controlled fixture item, not a forced call against removed world state.


## Step 36 v1 vertical-slice evidence runner

Added a read-only Step 36 dev evidence checker:
- `tools/check_mmo_step36_vertical_slice.py` verifies one applied pickup/equip/unequip sequence across JSONL evidence, `mmo_server_action_outbox`, `world_event_journal` and MySQL current-state projections.
- It checks that pickup rows are applied, journal events exist, picked item instances are character-owned/active, loose world entities are no longer active, and the unequipped item no longer remains in `character_equipment`.
- It can optionally attach runtime SQLite table/hash summaries and native save file hashes, but v1 does not yet perform a semantic native `.sav` replay comparison.
- This is evidence for the Step 35/36 vertical slice, not a global restore-parity pass. Do not mark required parity scenarios green only from a dev fixture.

## Step 36 v1.1 JSONL correlation implemented

Step36 evidence checker now correlates JSONL capture rows to applied outbox rows by a stable dev-only action fingerprint (`action_kind + target_key`).
This closes the replay-session-key warning where DB evidence passed but original client JSONL used an older idempotency prefix.

This is not production idempotency and not a substitute for full native `.sav + SQLite + MySQL` parity. It is a stricter evidence bridge between:

```text
client JSONL -> receiver JSONL -> outbox -> journal -> projection
```

## Step 36 v1.2 server JSONL recovery implemented

Step36 vertical-slice checker can now recover server JSONL evidence from `mmo_server_action_outbox.request_payload` when the receiver JSONL file is empty or missing. This closes the local evidence gap caused by lost/truncated JSONL files while preserving the rule that global parity is not passed until real native `.sav` + SQLite + MySQL restore artifacts converge.



## Step37 tool/server-boundary update

Step37 has started as a server-boundary/evidence-tool pass, not yet as a final C++ script hook pass:

- `tools/cleanup_mmo_tools.py` archives obsolete/Postgres/one-shot helper tools while keeping active MySQL/server-boundary validators.
- `tools/run_mmo_action_receiver.py` accepts and normalizes Step37 script/progression action kinds: `set_script_int`, `adjust_progression`, `apply_experience_reward`, `update_quest`, `set_known_dialog`.
- `tools/run_mmo_resolved_action_worker.py` dispatches those action kinds to existing MySQL procedures.
- `tools/check_mmo_step37_bookstand_script_xp.py` verifies outbox, journal, projection and optional JSONL/SQLite evidence for the bookstand/bookshelf one-shot XP slice.

Next C++ work remains real post-success script/progression capture around Daedalus execution. Do not mark `bookstand_script_xp` parity passed until the evidence comes from a real gameplay scenario, not only a manually prepared dev envelope.

## Step 37 C++ producer update

Step37 now has a C++ producer patch for player-owned script execution boundaries:
- `GameScript::exec` captures dialog/script effects after selected dialog execution.
- `GameScript::invokeItem` captures item-use script effects.
- `GameScript::useInteractive` captures mobsi/interactive script effects; this is the key path for bookstands/bookshelves/regals.
- The capture is disabled unless the semantic action sink is enabled and the actor is the player in a live world tick.
- The hook snapshots mutable global Daedalus INT values, player progression fields and known dialog/quest projections before the script call, then emits semantic actions after successful script execution.

Emitted Step37 action kinds:
- `set_script_int` for changed mutable global INT symbols.
- `adjust_progression` for player level/XP/LP/experience_next changes.
- `set_known_dialog` for newly known dialog info pairs.
- `update_quest` for quest status or entry-count changes.

This is still evidence capture, not final authority. Production remains `OpenGothic client -> MMO server -> MySQL`; the client does not own the database mutation.



## Step 38 C++ trade/combat/resource producer update

Step38 now starts as a large C++ producer/server-boundary pass after the green Step37 bookstand/script-XP E2E run:

- `Npc::buyItem` emits `trade_buy_from_npc` after successful inventory/gold mutation by the player.
- `Npc::sellItem` emits `trade_sell_to_npc` after successful inventory/gold mutation by the player.
- `Npc::shootBow` emits `consume_item` for one ranged ammunition unit after the bullet is created and ammunition is actually removed.
- `Npc::changeAttribute` emits player-related `consume_mana`, `apply_character_damage`, or `apply_world_entity_damage` after the attribute mutation and health checks settle.
- `Npc::onNoHealth` emits `mark_npc_dead` for a non-player NPC killed by the player.

The receiver and resolved worker understand these Step38 action kinds. Trade resolution is conservative: buy/sell only dispatches when NPC/world inventory or character inventory can be resolved uniquely to a DB item instance. Combat/resource dispatch maps to the existing MySQL procedures from steps 011..014.

This is still a dev evidence bridge, not final authority. Production remains `OpenGothic client -> MMO server -> MySQL`; the OpenGothic process never calls MySQL directly.



## Step 38 v1.1 resolver/projection-alignment update

Real Step38 C++ capture produced combat/resource JSONL, but the first MySQL replay exposed projection alignment gaps rather than hook failure:

- Step38 NPC hook keys use `npc:<world>:pid:<persistent_id>:sym:<symbol>`.
- Runtime SQLite/MySQL imports often store creature keys as `npc:<world>:<persistent_id>:<symbol>:<script_id>`.
- Character inventory stacks such as ammunition may not preserve the item object's transient persistent id in `item_instances.raw_payload`.

The resolved worker now handles these key-shape differences conservatively:

- NPC resolver accepts both Step38 hook keys and imported runtime NPC key aliases.
- Character item resolver first tries persistent-id matches, then falls back to symbol-only only when exactly one active character-owned stack can satisfy the amount.
- Ambiguous matches still fail. Do not invent UUIDs inside the worker.

For local E2E only, `tools/prepare_mmo_step38_dev_fixture.py` can seed/reactivate missing NPC and ammo rows from a captured Step38 JSONL after the receiver has created a dev server session. This is a projection-alignment fixture, not production authority and not final parity evidence.

## Step 38 fixture schema-order fix

The Step38 dev fixture now resolves its session context against the actual MySQL
schema instead of assuming `server_sessions.updated_at`. It chooses existing
ordering columns from `information_schema.columns`, preferring
`server_sessions.last_seen_at` / `login_at` and `mmo_server_action_outbox.requested_at`.
This fixes the local E2E fixture path without changing the production rule:
OpenGothic still emits semantic actions only; MySQL is reached through the
server/worker boundary.


## Step 38 stale combat replay handling

Step38 combat/resource evidence now distinguishes real server mutations from stale local replay consequences:
- `consume_item` E2E is green for ranged ammunition through outbox -> worker -> `mmo_consume_character_item` -> journal/projection.
- Full combat replay can contain additional local damage/death envelopes after an earlier server-side `mark_npc_dead` already made the target inactive.
- The dev worker must not force `mmo_apply_world_entity_damage` against inactive entities because the procedure correctly rejects that state. It may mark such stale replay rows as applied no-op with resolver metadata and no journal event.
- This is dev replay normalization, not production authority. A production server should accept player intents before applying damage/death, not blindly trust client-side consequence ordering.



## Step38 completion evidence and Step39 start

Real Step38 server-boundary evidence is now green for the dev E2E slices:
- combat/resource combined: damage, character damage, item/ammunition consumption and NPC death all replay through receiver -> outbox -> resolved worker -> MySQL procedures -> journal/projections; redundant post-death local damage is handled as an applied no-op with no journal event;
- trade focused: buy and sell each pass independently and as a combined buy+sell replay using `npc_trade_inventory` for vendor stock and character inventory/wallet projections for sell/buy validation.

Step39 starts movement/checkpoint ownership. The first implementation intentionally uses bounded periodic `character_checkpoint` envelopes, not per-frame movement writes. Production server authority is still future work: this pass only proves client capture -> server boundary -> `mmo_checkpoint_character_state(...)` -> `character_positions`/`character_stats` projection and checkpoint audit.

New C++ dev flag:
```text
-mmo-action-checkpoint-interval-ms <0|>=250>
```
`0` disables movement checkpoint envelopes. A non-zero value emits periodic player checkpoints only when the semantic action sink is enabled and the world is live.

## Step39 v2 movement checkpoint coalescing and evidence hardening

Step39 checkpoint capture is now less noisy and closer to production shape:
- `-mmo-action-checkpoint-min-distance <world-units>` suppresses stationary checkpoints until the player moved far enough.
- `-mmo-action-checkpoint-min-yaw-deg <degrees>` can emit a checkpoint for a meaningful facing change.
- `-mmo-action-checkpoint-force-interval-ms <ms>` is an optional keepalive so reconnect state is refreshed even if the character is idle.
- Checkpoint envelopes now carry the emit reason and the active checkpoint cadence/coalescing thresholds as diagnostics.
- The JSONL checker reports distance, stationary ratio, tick deltas, bounding box and reason counts.
- The MySQL checker verifies `outbox -> journal -> character_checkpoint_audit -> character_positions` and can require distinct positions.

This is still checkpoint authority, not final movement authority. Final movement/server authority still requires input proposals, server-side speed/collision/world-bound validation, interest management and replication snapshots.



## Step40 movement authority harness

Step39 v2 is now evidence-green for bounded checkpoint capture and MySQL persistence. Step40 starts the next server-first layer without changing the OpenGothic hot path:

- `tools/check_mmo_step40_movement_authority.py` treats `character_checkpoint` rows as server-side movement proposals.
- The validator accepts only a plausible ordered movement path and writes impossible jumps/teleports/out-of-bounds rows to a rejected JSONL artifact.
- `tools/run_mmo_step40_movement_authority_e2e.py` replays only the accepted movement stream through the existing receiver -> outbox -> resolved worker -> `mmo_checkpoint_character_state(...)` chain.
- `tools/build_mmo_step40_movement_authority_manifest.py` packages source capture, authority decision report, accepted/rejected streams, E2E result and MySQL checker result.

This is not final live movement authority yet. It is the offline/dev authority gate that must exist before replacing checkpoint consequence capture with real input proposals. The production target remains:

```text
client input/proposal -> MMO server authority validation -> accepted state mutation/checkpoint -> event journal/projection -> replication snapshot
```

Rules for interpreting Step40:
- Accepted checkpoint replay proves server-side sanity filtering plus bounded persistence.
- Rejected JSONL rows are useful evidence; they must not be silently persisted.
- A clean normal walking capture should pass with zero rejected rows using conservative speed/distance limits.
- Final authority still requires live input capture, server-side collision/world-bounds from navigation/world content, interest management and replication snapshots.

## Step40 v2 movement authority negative suite

Step40 positive authority evidence is now green for the real Step39 v2 capture:
normal walking checkpoint proposals are accepted by the server-side authority
harness and only the accepted stream is replayed through receiver -> outbox ->
resolved worker -> `mmo_checkpoint_character_state(...)`.

Step40 v2 adds hostile/negative movement evidence:
- `build_mmo_step40_movement_negative_corpus.py` creates deterministic mutated
  JSONL scenarios from a clean capture: X/Z teleport, vertical spike, tick/time
  reversal, outside world bounds and invalid position.
- `check_mmo_step40_movement_authority.py` can now be used in negative mode with
  `--allow-rejections`, `--min-rejected` and `--require-reject-reason`.
- `run_mmo_step40_movement_negative_suite.py` proves each hostile scenario is
  rejected or fails closed before persistence.
- `build_mmo_step40_movement_authority_final_manifest.py` combines the positive
  authority-gated MySQL evidence and the negative rejection suite.

Interpretation:
- A positive Step40 manifest proves normal movement can be accepted and persisted
  through the server boundary.
- A negative Step40 suite proves impossible movement is not silently persisted.
- This is still an offline/dev authority harness. Final live movement authority
  requires input proposals, server runtime movement integration, collision/world
  bounds from content and replication snapshots.

## Step41 movement proposal / intent harness

After Step40 positive + negative movement authority evidence, Step41 starts the transition from checkpoint consequences to server-validated movement proposals:
- C++ can emit `movement_proposal` envelopes with `from_tick/from_pos -> to_tick/to_pos` deltas using `-mmo-action-movement-proposal-*` flags.
- `movement_proposal` is not dispatched directly to MySQL. It represents a client proposal/intent and must pass a server authority validator first.
- `tools/check_mmo_step41_movement_proposal_jsonl.py` validates proposal shape, monotonic ticks, distance/speed/vertical/bounds constraints and can convert accepted proposals into `character_checkpoint` envelopes.
- `tools/run_mmo_step41_movement_proposal_e2e.py` replays only accepted server checkpoints through the existing Step39 MySQL checkpoint chain.

This is still not final live netcode. It is the first explicit contract split:
```text
client movement_proposal -> server authority gate -> accepted character_checkpoint -> MySQL projection
```
Rejected proposals must never reach the DB checkpoint procedure.

## Step42 fall-aware movement proposal authority

Step42 extends Step41 after real testing included a cliff fall. The movement
proposal payload now includes previous/current motion-state flags and previous
HP/mana, so the server-side validator can distinguish plausible gravity fall
from upward fly, teleport and unmarked vertical drops.

The Step42 checker keeps horizontal speed strict, applies separate upward and
fall-specific vertical envelopes, and adds continuity checks. A rejected proposal
therefore prevents later stale client proposals from being accepted as if the
server had followed the rejected position. Negative corpus coverage now includes
teleport, upward fly, unmarked drop, impossible marked fall, time reversal and
invalid position.

This is still offline/dev authority. The next production step is live proposal
transport with server correction/snapback/resync and replication snapshots.



## Step43 live MMO server boundary skeleton

Step43 starts the real `server/` process boundary. This is no longer only a loose
pair of dev tools. The new server process owns UDP intake, session/idempotency
filtering, fall-aware movement proposal validation, rejected-action evidence,
accepted checkpoint conversion and optional MySQL outbox enqueue.

New runtime path:

```text
OpenGothic semantic UDP
  -> server/mmo_server.py
  -> envelope/session/idempotency validation
  -> movement_proposal authority gate when applicable
  -> accepted character_checkpoint envelope
  -> optional mmo_server_action_outbox enqueue
  -> existing resolved MySQL worker/procedures
```

Important Step43 rules:
- `movement_proposal` is never directly persisted to MySQL.
- Accepted movement proposals are converted by the server into
  `character_checkpoint` rows.
- Rejected movement proposals are written to reject JSONL and must not be
  persisted.
- Non-movement semantic actions still pass through the same outbox contract as
  Step35..38.
- This is still a Python/dev server skeleton; it is intentionally authoritative
  at the process boundary but not yet the final high-performance C++ MMO shard.

New files:
- `server/mmo_server.py` wrapper.
- `server/mmo/actions.py` semantic envelope validation and DB payload mapping.
- `server/mmo/authority.py` stateful Step43 fall-aware live movement authority.
- `server/mmo/db.py` MySQL CLI bridge for login/outbox enqueue.
- `server/mmo/server.py` UDP server loop and orchestration.
- `tools/run_mmo_server.py` project-root wrapper.
- `tools/run_mmo_step43_server_smoke.py` no-MySQL smoke test.
- `tools/check_mmo_step43_server_live.py` artifact checker.

Recommended no-MySQL smoke:

```bash
python3 tools/run_mmo_step43_server_smoke.py \
  --output-dir runtime/step43_server_smoke \
  --session-key local-dev-PC_HERO_STEP43_SERVER_SMOKE
```

Recommended live capture without DB:

```bash
python3 tools/run_mmo_server.py \
  --bind 127.0.0.1:29777 \
  --accepted-jsonl runtime/mmo_server_actions_step43.jsonl \
  --rejected-jsonl runtime/mmo_server_rejects_step43.jsonl \
  --checkpoint-jsonl runtime/mmo_server_checkpoints_step43.jsonl \
  --summary-json runtime/mmo_server_step43_summary.json \
  --require-session local-dev-PC_HERO_STEP43 \
  --truncate \
  --require-motion-state-for-large-fall
```

Recommended live capture with DB enqueue:

```bash
python3 tools/run_mmo_server.py \
  --bind 127.0.0.1:29777 \
  --accepted-jsonl runtime/mmo_server_actions_step43.jsonl \
  --rejected-jsonl runtime/mmo_server_rejects_step43.jsonl \
  --checkpoint-jsonl runtime/mmo_server_checkpoints_step43.jsonl \
  --summary-json runtime/mmo_server_step43_summary.json \
  --require-session local-dev-PC_HERO_STEP43 \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo" \
  --account-name local-import \
  --character-key PC_HERO \
  --db-session-key local-dev-PC_HERO_STEP43 \
  --enqueue-outbox \
  --truncate \
  --require-motion-state-for-large-fall
```

Then run the existing resolved worker against the accepted server-created
checkpoint/outbox rows:

```bash
python3 tools/run_mmo_resolved_action_worker.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP43 \
  --max-actions 100
```

Step43 acceptance:
- no-MySQL smoke passes with accepted walking/fall proposals, rejected hostile
  teleport and generated checkpoint rows;
- live OpenGothic capture produces `movement_proposal` accepted rows and server
  `character_checkpoint` rows;
- MySQL mode enqueues only accepted server mutations;
- rejected proposals do not create checkpoint/outbox rows;
- Step39/40/41/42 offline tools stay useful as regression harnesses, but the
  new development direction is live server process ownership.


## Step44 live gameplay domain coverage

Step44 starts broad live-domain evidence after the Step43 server skeleton. The
goal is to stop thinking only in movement terms and verify mixed gameplay rows:
dialog, quest, script/progression, inventory pickup/equipment, item drop, trade,
resource use, combat damage and NPC death.

New rules:
- A long live session must keep going after one resolver/projection mismatch; use
  `--continue-on-error` for evidence runs, but keep failures visible.
- Repeated local pickups against stale MySQL projections may be aligned with
  `prepare_mmo_dispatch_dev_fixture.py` for dev-only E2E, never as production
  truth.
- `drop_character_item` is capture-only until a canonical MySQL
  `mmo_drop_character_item(...)` procedure/projection mutation is added.
- The Step44 checker reports coverage and DB gaps separately so missing hooks,
  missing gameplay, resolver mismatches and missing DB procedures are not mixed
  together.


## Step45 world-AI weapon/loot gap closure

Step45 follows real Step44 testing. The live server path accepted broad gameplay domains, but three MMO-relevant semantics were still missing:
- weapon ready/holster state, important because Gothic NPCs react to drawn/removed weapons;
- corpse/dead-NPC looting as a distinct semantic action instead of only a generic transfer;
- NPC-vs-NPC damage/death, for cases such as a guard killing a wolf that attacked sheep.

Step45 adds C++ capture for `ready_weapon`, `holster_weapon`, `loot_npc_inventory`, and broadens world-AI combat/death capture. It also hardens UDP diagnostics for invalid non-UTF-8 JSON packets and escapes non-ASCII display bytes in C++ JSON payloads.

These new weapon/loot actions are capture-only in the resolved worker until canonical MySQL procedures are added. Existing combat/death procedures remain valid for world-entity damage and NPC death.


## Step46 consumables / world clock / AI context cleanup

Step46 follows live Step45 testing. New real gaps:
- eating looted meat restored HP but emitted no server-side item-consumption row;
- NPC weapon holster emitted repeated semantic rows while AI kept retrying the same state;
- sleeping to the next morning changed world time without a server-visible semantic row;
- waypoint/routine data and NPC-to-NPC speech needed a clear production interpretation.

Step46 changes:
- `Inventory::use(...)` now measures item stack count before/after script `on_state` execution and emits `consume_item` when an item was actually consumed. This covers looted meat/food consumed by the player.
- positive player HP/mana deltas from `Npc::changeAttribute(...)` emit `character_resource_delta` capture rows, so the server sees the healing consequence of food even though a final MySQL resource-delta procedure is not added yet.
- `World::setDayTime(...)` emits `world_time_changed`, which covers bed/sleep time skips such as sleeping until morning.
- weapon state capture now keeps a small fixed-size actor/state cache so repeated AI calls to holster the same already-seen state do not spam `holster_weapon` rows.

Current interpretation:
- `consume_item` uses the existing MySQL `mmo_consume_character_item(...)` worker path.
- `character_resource_delta` and `world_time_changed` are capture-only in Step46 until canonical MySQL procedures/projections are added.
- Waypoints/routines are currently bootstrap/read-model data in SQLite/MySQL projections. They are not emitted as live semantic events every tick. Server authority should use them for AI/routine validation, not persist every path step.
- NPC-to-NPC speech/SVM audio is mostly transient presentation. Persist only durable consequences such as dialog flags, quest/script/global changes, AI relation checkpoints, weapon reactions, combat/death, and inventory/resource mutations.

## Step47 interactive / mobsi state producer

Step47 adds live server evidence for Gothic interactives such as fireplaces, levers, switches, doors and mobsi state changes:
- `Interactive::attach(...)` emits `use_interactive` after accepted player use.
- `Interactive::setState(...)` emits `update_interactive_state` when state/locked/cracked changes.
- Payload keys follow the runtime/MySQL import key: `mobsi:<world>:<slot_id>:<vob_id>:<focus_name>`.
- Server normalization and the resolved worker now understand `use_interactive` and `update_interactive_state`.
- `update_interactive_state` calls existing `mmo_update_interactive_state(...)`; `use_interactive` is capture-only evidence until a canonical intent procedure exists.

This closes the Xardas tower fireplace/hidden-room gap where a visible world mutation had no live server entry.



## Step48 interactive trigger/mover filter

Step48 fixes the Step47 over-capture found in a real Xardas tower fireplace test. `Interactive::setState` was treating bootstrap/TA mobsi materialization as live gameplay and produced hundreds of `update_interactive_state` rows. The Step48 hook now requires an explicit player cause: direct player actor, recent use of the same interactive, or a short recent player interactive world window.

Step48 also adds capture-only evidence for the trigger/mover chain that normally opens hidden gates, grates and moving doors: `trigger_event` and `mover_state_changed`. These are not canonical DB procedures yet; they are server-boundary evidence so the next DB procedure design can persist mover state without guessing from snapshots.

Acceptance: using a fireplace/lever should produce a small set of `use_interactive`/`update_interactive_state` actions and, when a mover chain is involved, `trigger_event`/`mover_state_changed`, without the hundreds of unrelated mobsi initialization rows seen in Step47.



## Step49 server bootstrap / NPC navigation probe

Step49 adds read-only probes for the next server ownership questions:
- runtime SQLite waypoint/routine/navigation/NPC-relation evidence;
- MySQL current-projection readiness for server restart materialization;
- explicit procedure gaps for trigger/mover, weapon state, world time/resource deltas, teleport/world transition, item/container respawn and LP spending.

This is intentionally not full authority. It answers whether the captured/projection data is sufficient to design the server runtime model without turning SQLite into production truth.

Key rule: server restart must load current projections/event-journal truth, not respawn everything from baseline. Baseline is for templates, validation and scheduled respawn policy, not login-time reset.
