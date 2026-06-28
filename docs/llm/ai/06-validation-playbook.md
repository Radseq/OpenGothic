# 06 Validation Playbook

Use validation to prevent fake readiness.

Current expected status after DB Step 30:
```text
database_status = complete
mmo_status      = blocked
errors          = 0
warnings        = parity scenarios not passed
```
`blocked` is correct until external evidence exists.

Before any hook/server change:
- Run existing MySQL Step 23..30 checker.
- Confirm `v_mmo_database_remaining_work_final` shows only external blockers: C++ hooks and native/SQLite/MySQL parity.

Useful MySQL checks:
```sql
SELECT area, requirement_key, status, severity, problem_count, details
FROM v_mmo_database_remaining_work_final;

SELECT entity_kind, COUNT(*) FROM world_entity_state GROUP BY entity_kind ORDER BY COUNT(*) DESC;

SELECT event_type,event_class,source,COUNT(*)
FROM world_event_journal GROUP BY event_type,event_class,source;

SELECT * FROM v_strict_replay_latest_errors LIMIT 20;
SELECT * FROM v_restore_parity_artifact_failures LIMIT 20;
```

Runtime SQLite checks:
- Use `tools/check_runtime_sqlite.py` and `tools/audit_runtime_sqlite.py`; do not trust GUI rendering alone for encoding or schema quality.
- Treat invariant errors as persistence defects. Treat missing optional event scenarios as coverage gaps until a test exercises them.

Parity scenarios must be real:
1. `bookstand_script_xp`: one-shot script flag plus XP/LP/stat/progression evidence.
2. `world_item_pickup`: world item removed, item instance moved to character, inventory projection matches.
3. `equip_unequip`: inventory/equipment slots and derived stat changes.
4. `container_change`: container inventory + interactive state.
5. `quest_progress`: quest status/entries preserved.
6. `dialog_consumed`: visible choice/update vs executed/known dialog distinction.
7. `npc_killed`: hp/dead/lifecycle/script side effects.
8. `chapter_change`: `KAPITEL` durable value, not only `IntroduceChapter` presentation.
9. `save_restart_load`: native `.sav`, SQLite slot snapshot and MySQL projection converge.

Performance checks for hooks:
- Disabled MMO bridge: should compile to no-op/cheap branch and cause no visible regression.
- Enabled dev bridge: game thread only snapshots/enqueues; no socket/DB blocking.
- Bounded queue overflow must be visible and safe: fail fast in strict tests, drop only diagnostics in non-strict mode.

Server validation checks:
- Duplicate idempotency key returns existing event/result.
- Reordered/duplicated client packets cannot duplicate items/gold/XP.
- Server rejects invalid actor/world/target/session, stale world instance, impossible amount/slot, and forbidden display-name identity.

## C++ semantic hook smoke

Run the game with dev JSONL capture:

```bash
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-action-jsonl runtime/mmo_actions.jsonl \
  -mmo-action-session-key local-dev-PC_HERO \
  -mmo-action-queue-capacity 8192
```

Then perform actions in game:
- pickup a world item;
- equip/unequip an item;
- move item between inventory/container if possible.

Validate emitted envelopes:

```bash
python3 tools/check_mmo_semantic_action_jsonl.py runtime/mmo_actions.jsonl \
  --require-kind pickup_world_item
```

Expected properties:
- each line is valid JSON;
- `idempotency_key` is non-empty and unique per local action;
- disabled mode without `-mmo-action-jsonl` emits nothing and should behave as before;
- JSONL is evidence only. Server acceptance/parity is a later step.



## Hook bootstrap-noise regression check

After JSONL capture, `tools/check_mmo_semantic_action_jsonl.py` prints `tick0`, `tick_gt0`, and actor buckets.

Expected for live player-action smoke after bootstrap filter:
- `tick0` should be `0` or near-zero for explicit gameplay action tests.
- `actor.npc` should not dominate player pickup/equip tests.
- `pickup_world_item`, `equip_character_item`, `unequip_character_item` should correspond to actions intentionally performed by the player, not thousands of load-time NPC equipment operations.

If thousands of events appear immediately on startup, treat that as a hook placement/filter bug, not gameplay evidence.



## Step 33 local UDP receiver smoke

Start receiver in terminal 1:

```bash
python3 tools/run_mmo_action_receiver.py \
  --bind 127.0.0.1:29777 \
  --jsonl runtime/mmo_server_actions.jsonl \
  --require-session local-dev-PC_HERO \
  --truncate
```

Run game in terminal 2 with both local evidence and server-boundary transport:

```bash
rm -f runtime/mmo_actions.jsonl runtime/mmo_server_actions.jsonl
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-action-jsonl runtime/mmo_actions.jsonl \
  -mmo-action-udp 127.0.0.1:29777 \
  -mmo-action-session-key local-dev-PC_HERO \
  -mmo-action-queue-capacity 8192
```

Validate both files after pickup/equip actions:

```bash
python3 tools/check_mmo_semantic_action_jsonl.py runtime/mmo_actions.jsonl
python3 tools/check_mmo_semantic_action_jsonl.py runtime/mmo_server_actions.jsonl
```

Expected: local and receiver JSONL have the same action kinds and no bootstrap noise. UDP is dev-only and may lose packets under stress; production networking needs reliable ordering/ack/retry later.

## Step 35 receiver -> MySQL outbox smoke

Start receiver with DB enqueue in terminal 1:

```bash
python3 tools/run_mmo_action_receiver.py \
  --bind 127.0.0.1:29777 \
  --jsonl runtime/mmo_server_actions.jsonl \
  --reject-jsonl runtime/mmo_server_rejects.jsonl \
  --require-session local-dev-PC_HERO \
  --mysql-url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --account-name local-import \
  --character-key PC_HERO \
  --db-session-key local-dev-PC_HERO \
  --enqueue-outbox \
  --truncate
```

Run game in terminal 2:

```bash
rm -f runtime/mmo_actions.jsonl
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-action-jsonl runtime/mmo_actions.jsonl \
  -mmo-action-udp 127.0.0.1:29777 \
  -mmo-action-session-key local-dev-PC_HERO \
  -mmo-action-queue-capacity 8192
```

Do not delete `runtime/mmo_server_actions.jsonl` after the receiver has started. Use receiver `--truncate` instead.
The receiver opens the file per accepted write now, so accidental deletion is less dangerous, but the safe workflow is still: truncate/start receiver first, then run game.

Validate local and server evidence:

```bash
python3 tools/check_mmo_semantic_action_jsonl.py runtime/mmo_actions.jsonl
python3 tools/check_mmo_semantic_action_jsonl.py runtime/mmo_server_actions.jsonl
```

Validate outbox rows:

```bash
python3 tools/check_mmo_action_receiver_outbox.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO \
  --require-kind pickup_world_item \
  --require-kind equip_character_item \
  --require-kind unequip_character_item
```

Duplicate/idempotency smoke without relaunching the game:

```bash
python3 tools/replay_mmo_actions_to_receiver.py runtime/mmo_actions.jsonl --to 127.0.0.1:29777 --repeat 2
python3 tools/check_mmo_action_receiver_outbox.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO
```

Expected interpretation:
- Receiver duplicates should not create duplicate accepted JSONL rows in one receiver process.
- MySQL outbox uniqueness on `(world_instance_id, idempotency_key)` should return the existing action/status on retry.
- `dispatch_ready=false` in outbox payload is acceptable for current pickup/equip envelopes because DB UUID resolution is not implemented yet.
- Do not run the existing MySQL outbox worker on these rows as if they were procedure-ready until a key resolver fills `item_instance_id` and other required DB arguments.

## Step 35 v2 resolved dispatch smoke

After receiver enqueue, run the resolved worker:

```bash
python3 tools/run_mmo_resolved_action_worker.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --worker-id dev-resolved-worker \
  --max-actions 10
```

Then verify DB-visible dispatch:

```bash
python3 tools/check_mmo_action_dispatch_results.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO \
  --require-applied-kind pickup_world_item
```

For the first clean test, prefer only one pickup action before equip/unequip. If equip/unequip fail with unresolved or ambiguous item instance, inspect the item import/projection keys instead of forcing UUIDs manually.

Useful SQL after a dispatch run:

```sql
SELECT action_kind,status,event_uuid,last_error_code,last_error_message
FROM v_server_action_outbox
WHERE idempotency_key LIKE 'local-dev-PC_HERO:%'
ORDER BY updated_at DESC
LIMIT 20;

SELECT event_type,event_class,source,entity_key,subject_key,idempotency_key
FROM world_event_journal
WHERE idempotency_key LIKE 'local-dev-PC_HERO:%'
ORDER BY event_seq DESC
LIMIT 20;

SELECT audit_type, world_item_entity_key, BIN_TO_UUID(item_instance_id,1), amount
FROM world_item_audit
WHERE idempotency_key LIKE 'local-dev-PC_HERO:%'
ORDER BY created_at DESC
LIMIT 20;
```

Expected interpretation:
- `pickup_world_item` can become fully applied if the world item entity key resolves to exactly one active MySQL `world_entity_state` item.
- `equip_character_item` can apply after pickup if the resolver finds exactly one active character-owned item instance by item symbol and source persistent id.
- Resolver failures are useful evidence: they indicate key/import mismatch or ambiguous item semantics, not a reason to bypass server authority.

## Step 35 v2.1 resolved worker diagnostics

Run the resolved worker with an idempotency/session prefix so it does not claim stale rows from older local smoke tests:

```bash
python3 tools/run_mmo_resolved_action_worker.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --worker-id dev-resolved-worker \
  --session-key local-dev-PC_HERO_STEP35V3 \
  --max-actions 10
```

The worker stops on the first failure by default. This is intentional for dependent sequences: if pickup fails, later equip/unequip should not be applied as if the item existed in the server projection. Use `--continue-on-error` only for broad diagnostics.

If a previous attempt failed and you deliberately want to retry the same prefix:

```bash
python3 tools/run_mmo_resolved_action_worker.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP35V3 \
  --reset-matching-failed \
  --dry-run
```

Read-only resolver inspection:

```bash
python3 tools/inspect_mmo_action_resolution.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP35V3
```

Interpretation:
- `world item resolved but is not active` means the client JSONL and MySQL world projection are not aligned for that world item, or the item was already consumed by an earlier DB action.
- Do not force pickup/equip dispatch in that state. Use a fresh matching projection/session/action set before marking `world_item_pickup` or `equip_unequip` as parity evidence.


## Step 36 vertical-slice evidence check

After a Step 35 resolved dispatch run has `applied` pickup/equip/unequip rows, collect a read-only evidence artifact:

```bash
python3 tools/check_mmo_step36_vertical_slice.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP35V2 \
  --client-jsonl runtime/mmo_actions.jsonl \
  --server-jsonl runtime/mmo_server_actions_step35v2.jsonl \
  --sqlite runtime/g2notr.sqlite \
  --require-two-pickups \
  --output runtime/mmo_step36_vertical_slice_STEP35V2.json
```

Expected for the current pickup/equip/unequip slice:
- `mmo_server_action_outbox` has applied rows for `pickup_world_item`, `equip_character_item`, and `unequip_character_item`;
- `world_event_journal` has matching server events `world_item_picked_up`, `character_item_equipped`, and `character_item_unequipped`;
- picked item instances are now `owner_type='character'` and active;
- matching loose world item entities are no longer active;
- after unequip, the equipment item is still in character inventory and not in active `character_equipment`.

Interpretation:
- `[OK]` means the vertical server/DB projection slice is internally consistent.
- It is not equivalent to full native `.sav` + SQLite + MySQL restore parity. Native save hashes and SQLite summaries are supplemental until a real semantic replay/load comparison exists.

## Step 36 v1.1 JSONL fingerprint correlation

If Step 36 passes DB projection checks but warns that client/server JSONL has no matching rows, check whether the replay changed the session key prefix.

The Step36 checker now correlates JSONL to applied outbox rows by a non-authoritative evidence fingerprint:

```text
action_kind + target_key
```

Use strict correlation when you expect JSONL files to be present:

```bash
python3 tools/check_mmo_step36_vertical_slice.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP35V2 \
  --client-jsonl runtime/mmo_actions.jsonl \
  --server-jsonl runtime/mmo_server_actions_step35v2.jsonl \
  --sqlite runtime/g2notr.sqlite \
  --require-two-pickups \
  --require-jsonl-correlation \
  --output runtime/mmo_step36_vertical_slice_STEP35V2.json
```

Interpretation:
- `session_rows > 0` means exact idempotency/session prefix matched.
- `session_rows == 0` and `fingerprint_rows > 0` means replay-session rewrite happened, but the same gameplay actions were found.
- `fingerprint_rows == 0` means JSONL evidence is missing or from a different scenario.

Do not use this fingerprint as production idempotency. Production idempotency remains server-owned and stored in the DB/outbox/journal.

## Step 36 v1.2 server JSONL recovery

If strict Step36 check fails only because `server_jsonl` has zero rows, but outbox/journal/projection passed, recover receiver evidence from outbox request payload:

```bash
python3 tools/check_mmo_step36_vertical_slice.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP35V2 \
  --client-jsonl runtime/mmo_actions.jsonl \
  --server-jsonl runtime/mmo_server_actions_step35v2.jsonl \
  --sqlite runtime/g2notr.sqlite \
  --require-two-pickups \
  --require-jsonl-correlation \
  --recover-server-jsonl-from-outbox \
  --write-recovered-server-jsonl runtime/mmo_server_actions_step35v2.recovered.jsonl \
  --output runtime/mmo_step36_vertical_slice_STEP35V2.json
```

This should be treated as recovered server-boundary evidence, not as a substitute for a future clean receiver JSONL run.

## Step 36 v1.3 evidence package

After a Step36 vertical-slice check passes, package the evidence so the exact JSON artifact, client JSONL, server/recovered server JSONL and SQLite summary can be archived together:

```bash
python3 tools/package_mmo_step36_evidence.py \
  --session-key local-dev-PC_HERO_STEP35V2 \
  --artifact runtime/mmo_step36_vertical_slice_STEP35V2.json \
  --client-jsonl runtime/mmo_actions.jsonl \
  --server-jsonl runtime/mmo_server_actions_step35v2.jsonl \
  --recovered-server-jsonl runtime/mmo_server_actions_step35v2.recovered.jsonl \
  --sqlite runtime/g2notr.sqlite \
  --output-dir runtime/evidence/step36_STEP35V2 \
  --zip runtime/evidence/step36_STEP35V2.zip \
  --strict
```

The package is evidence for the current vertical slice only. It is not full restore parity until a fresh run includes native `.sav`, SQLite save-slot and MySQL projection comparison from the same start state.



## Step37 script/progression smoke

After capturing and dispatching a bookstand/bookshelf script/progression session:

```bash
python3 tools/check_mmo_step37_bookstand_script_xp.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP37_BOOKSTAND \
  --client-jsonl runtime/mmo_actions_step37_bookstand.jsonl \
  --server-jsonl runtime/mmo_server_actions_step37_bookstand.jsonl \
  --sqlite runtime/g2notr.sqlite \
  --require-jsonl-correlation \
  --output runtime/mmo_step37_bookstand_script_xp.json
```

Required evidence:

- applied `set_script_int` outbox action;
- applied `adjust_progression` or `apply_experience_reward` outbox action;
- `character_script_int_set` journal event;
- `character_progression_adjusted` journal event;
- matching `character_script_state` projection row;
- no failed/dead-letter Step37 outbox rows;
- no duplicate idempotency-key application.

`status=passed` from this checker is Step37 vertical-slice evidence only. It does not mean full native `.sav` + SQLite save-slot + MySQL restore parity is green.

## Step37 C++ hook validation

After applying the Step37 C++ hook patch, first validate client-side JSONL before touching MySQL:

```bash
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-action-jsonl runtime/mmo_client_actions_step37_script_xp.jsonl \
  -mmo-action-session-key local-dev-PC_HERO_STEP37_SCRIPT_XP
```

In game, trigger a real player-owned script source such as a bookstand/bookshelf/regal or a usable book that awards XP only once. Then check the local action stream:

```bash
python3 tools/check_mmo_step37_script_jsonl.py \
  --jsonl runtime/mmo_client_actions_step37_script_xp.jsonl \
  --session-key local-dev-PC_HERO_STEP37_SCRIPT_XP \
  --require-script-int \
  --require-xp \
  --output runtime/mmo_step37_script_jsonl_check.json
```

Expected minimum evidence:
- at least one `set_script_int` row for the one-shot script flag;
- at least one `adjust_progression` row with non-zero `experience_delta` if the interaction awarded XP;
- no duplicate idempotency keys;
- valid payload shape for receiver/worker dispatch.

Only after this client JSONL evidence is green should the same action stream be sent through the receiver/resolved worker and verified with `check_mmo_step37_bookstand_script_xp.py` against MySQL.



## Step 38 trade/combat/resource validation

Run the game with local JSONL capture and perform one or more player actions:

- buy an item from an NPC;
- sell an item to an NPC;
- shoot bow/crossbow to consume ammunition;
- spend mana or take damage;
- kill a weak NPC/creature for lifecycle evidence.

Local JSONL check:

```bash
python3 tools/check_mmo_step38_trade_combat_jsonl.py \
  --jsonl runtime/mmo_client_actions_step38_trade_combat.jsonl \
  --session-key local-dev-PC_HERO_STEP38_TRADE_COMBAT \
  --require-resource \
  --require-combat \
  --output runtime/mmo_step38_trade_combat_jsonl_check.json
```

E2E server-boundary/MySQL replay from a captured JSONL file:

```bash
python3 tools/run_mmo_step38_trade_combat_e2e.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --client-jsonl runtime/mmo_client_actions_step38_trade_combat.jsonl \
  --session-key local-dev-PC_HERO_STEP38_TRADE_COMBAT_E2E \
  --output runtime/mmo_step38_trade_combat_e2e.json \
  --checker-output runtime/mmo_step38_trade_combat_mysql_e2e.json \
  --reset-matching-failed
```

Focused E2E can be done with repeated `--require-kind`, for example only `consume_item` or `apply_world_entity_damage`, when trade inventory resolution is not aligned with the imported MySQL projection.

Interpretation:

- `consume_mana`, `apply_character_damage`, `apply_world_entity_damage`, `consume_item`, and `mark_npc_dead` can become applied if the DB projection resolves the target/item.
- `trade_buy_from_npc` intentionally fails if the vendor inventory item is not uniquely present in `world_inventory` for the resolved NPC key.
- Do not force UUIDs manually. Resolver failure means the runtime action set and MySQL projection are not aligned enough for server authority.



## Step 38 resolver diagnostics and dev fixture

If Step38 local JSONL passes but MySQL E2E fails with resolver messages such as:

```text
NPC/world entity not found for key='npc:newworld.zen:pid:258:sym:12469'
character item instance not found for symbol=7083 pid=6
```

interpret this as runtime/MySQL projection mismatch first, not as C++ hook failure.

Preferred retry after applying the resolver update:

```bash
python3 tools/run_mmo_step38_trade_combat_e2e.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --client-jsonl runtime/mmo_client_actions_step38_trade_combat.jsonl \
  --session-key local-dev-PC_HERO_STEP38_TRADE_COMBAT_E2E \
  --output runtime/mmo_step38_trade_combat_e2e.json \
  --checker-output runtime/mmo_step38_trade_combat_mysql_e2e.json \
  --reset-matching-failed
```

If the projection is still missing local runtime rows and the goal is only to prove the stored-procedure chain, use the dev-only fixture:

```bash
python3 tools/run_mmo_step38_trade_combat_e2e.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --client-jsonl runtime/mmo_client_actions_step38_trade_combat.jsonl \
  --session-key local-dev-PC_HERO_STEP38_TRADE_COMBAT_E2E \
  --output runtime/mmo_step38_trade_combat_e2e.json \
  --checker-output runtime/mmo_step38_trade_combat_mysql_e2e.json \
  --reset-matching-failed \
  --prepare-dev-fixture
```

Standalone dry-run/apply fixture flow:

```bash
python3 tools/prepare_mmo_step38_dev_fixture.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP38_TRADE_COMBAT_E2E \
  --client-jsonl runtime/mmo_client_actions_step38_trade_combat_e2e.jsonl \
  --output runtime/mmo_step38_dev_fixture.dry_run.json

python3 tools/prepare_mmo_step38_dev_fixture.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP38_TRADE_COMBAT_E2E \
  --client-jsonl runtime/mmo_client_actions_step38_trade_combat_e2e.jsonl \
  --output runtime/mmo_step38_dev_fixture.json \
  --apply
```

Rules:

- The fixture may insert or reactivate NPC rows and seed missing ammunition stacks only for a local dev replay.
- It must never be counted as full native `.sav + SQLite + MySQL` parity.
- A production server should start from authoritative projections or reject stale/missing client targets, not repair them from client JSONL.

## Step38 dev fixture schema-order retry

If `--prepare-dev-fixture` fails with `Unknown column 'ss.updated_at'`, apply the
Step38 fixture schema-order fix and rerun the same command. The fixed fixture
records the session lookup source plus selected order columns in
`runtime/mmo_step38_dev_fixture.json`. Expected local schema columns are
`server_sessions.last_seen_at` / `login_at` and
`mmo_server_action_outbox.requested_at`.

Recommended retry:

```bash
python3 tools/run_mmo_step38_trade_combat_e2e.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --client-jsonl runtime/mmo_client_actions_step38_trade_combat.jsonl \
  --session-key local-dev-PC_HERO_STEP38_TRADE_COMBAT_E2E \
  --output runtime/mmo_step38_trade_combat_e2e.json \
  --checker-output runtime/mmo_step38_trade_combat_mysql_e2e.json \
  --reset-matching-failed \
  --prepare-dev-fixture
```

For the ammo-only slice, keep `--require-kind consume_item` and use the
`STEP38_AMMO_E2E` session key.


## Step 38 combat/resource replay checks

A valid Step38 partial pass may now be split:
- Ammo/resource slice: require `consume_item` and `character_item_consumed` journal rows.
- Combat/death slice: require at least one `world_entity_damage_applied` and one `npc_marked_dead` journal row, no failed outbox rows, and allow stale post-death local damage/death envelopes to be applied as no-op rows with `event_emitted=false`.

Do not count no-op stale rows as new gameplay mutations. They only prove the dev replay can continue after the authoritative projection has already moved the target to an inactive/dead lifecycle state.



## Step39 movement/checkpoint smoke

Capture bounded movement/checkpoints:
```bash
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-action-jsonl runtime/mmo_client_actions_step39_movement.jsonl \
  -mmo-action-session-key local-dev-PC_HERO_STEP39_MOVE \
  -mmo-action-queue-capacity 8192 \
  -mmo-action-checkpoint-interval-ms 1000
```

Walk for a few seconds, then exit to flush JSONL. Validate local capture:
```bash
python3 tools/check_mmo_step39_movement_jsonl.py \
  --jsonl runtime/mmo_client_actions_step39_movement.jsonl \
  --session-key local-dev-PC_HERO_STEP39_MOVE \
  --min-rows 2 \
  --require-position-change \
  --output runtime/mmo_step39_movement_jsonl_check.json
```

Replay through the server boundary and MySQL checkpoint procedure:
```bash
python3 tools/run_mmo_step39_movement_e2e.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --client-jsonl runtime/mmo_client_actions_step39_movement.jsonl \
  --session-key local-dev-PC_HERO_STEP39_MOVE_E2E \
  --output runtime/mmo_step39_movement_e2e.json \
  --checker-output runtime/mmo_step39_movement_mysql_e2e.json \
  --reset-matching-failed
```

This is checkpoint evidence only. Do not treat it as full movement authority, collision authority, or replication.

## Step39 v2 coalesced movement/checkpoint validation

Capture fewer, higher-quality movement checkpoints:

```bash
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-action-jsonl runtime/mmo_client_actions_step39_movement_v2.jsonl \
  -mmo-action-session-key local-dev-PC_HERO_STEP39_MOVE_V2 \
  -mmo-action-queue-capacity 8192 \
  -mmo-action-checkpoint-interval-ms 1000 \
  -mmo-action-checkpoint-min-distance 75 \
  -mmo-action-checkpoint-min-yaw-deg 15 \
  -mmo-action-checkpoint-force-interval-ms 5000
```

Check local evidence:

```bash
python3 tools/check_mmo_step39_movement_jsonl.py \
  --jsonl runtime/mmo_client_actions_step39_movement_v2.jsonl \
  --session-key local-dev-PC_HERO_STEP39_MOVE_V2 \
  --min-rows 2 \
  --require-position-change \
  --min-total-distance 100 \
  --max-stationary-ratio 0.35 \
  --min-tick-delta 250 \
  --output runtime/mmo_step39_movement_v2_jsonl_check.json
```

Replay through server boundary and MySQL, optionally with replay-side coalescing as a safety valve for older noisy captures:

```bash
python3 tools/run_mmo_step39_movement_e2e.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --client-jsonl runtime/mmo_client_actions_step39_movement_v2.jsonl \
  --session-key local-dev-PC_HERO_STEP39_MOVE_V2_E2E \
  --output runtime/mmo_step39_movement_v2_e2e.json \
  --checker-output runtime/mmo_step39_movement_v2_mysql_e2e.json \
  --reset-matching-failed \
  --require-position-change \
  --coalesce-min-distance 75 \
  --coalesce-force-tick-delta 5000
```

Build the compact manifest:

```bash
python3 tools/build_mmo_step39_movement_manifest.py \
  --session-key local-dev-PC_HERO_STEP39_MOVE_V2_E2E \
  --client-jsonl runtime/mmo_client_actions_step39_movement_v2.jsonl \
  --jsonl-check runtime/mmo_step39_movement_v2_jsonl_check.json \
  --e2e runtime/mmo_step39_movement_v2_e2e.json \
  --mysql-check runtime/mmo_step39_movement_v2_mysql_e2e.json \
  --output runtime/mmo_step39_movement_v2_manifest.json
```

A green manifest means the checkpoint stream is captured, coalesced, replayed through the server boundary, written by `mmo_checkpoint_character_state(...)`, audited and reflected in `character_positions`. It still does not prove collision authority or live replication.



## Step40 movement authority validation

After a green Step39 v2 capture, run the server-side authority gate before replaying movement into MySQL:

```bash
python3 tools/check_mmo_step40_movement_authority.py \
  --jsonl runtime/mmo_client_actions_step39_movement_v2.jsonl \
  --session-key local-dev-PC_HERO_STEP39_MOVE_V2 \
  --accepted-jsonl runtime/mmo_step40_movement_authority.accepted.jsonl \
  --rejected-jsonl runtime/mmo_step40_movement_authority.rejected.jsonl \
  --output runtime/mmo_step40_movement_authority.json \
  --min-accepted 2 \
  --require-position-change \
  --max-step-distance 2500 \
  --max-horizontal-speed 2500 \
  --max-vertical-speed 2500 \
  --max-vertical-delta 1600
```

Expected for a normal walking capture:
- `status=passed`;
- accepted rows >= 2;
- rejected rows = 0;
- `position_changed=true`;
- max segment distance/speed under configured limits.

Full authority-gated replay into MySQL:

```bash
python3 tools/run_mmo_step40_movement_authority_e2e.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --client-jsonl runtime/mmo_client_actions_step39_movement_v2.jsonl \
  --source-session-key local-dev-PC_HERO_STEP39_MOVE_V2 \
  --session-key local-dev-PC_HERO_STEP40_MOVE_AUTHORITY_E2E \
  --authority-output runtime/mmo_step40_movement_authority.json \
  --accepted-jsonl runtime/mmo_step40_movement_authority.accepted.jsonl \
  --rejected-jsonl runtime/mmo_step40_movement_authority.rejected.jsonl \
  --e2e-output runtime/mmo_step40_movement_authority_e2e.json \
  --mysql-check-output runtime/mmo_step40_movement_authority_mysql_e2e.json \
  --manifest-output runtime/mmo_step40_movement_authority_manifest.json \
  --output runtime/mmo_step40_movement_authority_run.json \
  --min-accepted 2 \
  --require-position-change \
  --reset-matching-failed \
  --max-replay-rows 20 \
  --coalesce-min-distance 75 \
  --coalesce-force-tick-delta 5000
```

Interpretation:
- The authority report is the server-side movement decision artifact.
- The accepted JSONL is the only movement stream that should be replayed into MySQL.
- The rejected JSONL is an explicit anti-cheat/authority artifact, not a failure by itself when `--allow-rejections` is deliberately used for hostile tests.
- The Step40 manifest is green only when authority validation, accepted replay and MySQL checkpoint evidence are all green.

## Step40 movement authority negative suite

After the positive Step40 authority-gated E2E is green, build and run hostile
movement scenarios from the same clean capture:

```bash
python3 tools/run_mmo_step40_movement_negative_suite.py \
  --jsonl runtime/mmo_client_actions_step39_movement_v2.jsonl \
  --session-key local-dev-PC_HERO_STEP39_MOVE_V2 \
  --output-dir runtime/step40_movement_negative_suite \
  --output runtime/mmo_step40_movement_negative_suite.json \
  --min-accepted 2 \
  --max-step-distance 2500 \
  --max-horizontal-speed 2500 \
  --max-vertical-speed 2500 \
  --max-vertical-delta 1600
```

Expected result:
- `teleport_xz` rejects at least one row with `step_distance_too_large`;
- `vertical_spike` rejects at least one row with `vertical_delta_too_large`;
- `time_reversal` rejects at least one row with `non_monotonic_tick`;
- `outside_world_bounds` rejects at least one row with `outside_world_bounds`;
- `invalid_position` rejects at least one row with `invalid_position`;
- accepted rows remain enough to prove the validator can continue after dropping
  a hostile proposal;
- no hostile scenario should be replayed into MySQL.

Build the final positive+negative Step40 manifest:

```bash
python3 tools/build_mmo_step40_movement_authority_final_manifest.py \
  --source-session-key local-dev-PC_HERO_STEP39_MOVE_V2 \
  --positive-manifest runtime/mmo_step40_movement_authority_manifest.json \
  --negative-suite runtime/mmo_step40_movement_negative_suite.json \
  --output runtime/mmo_step40_movement_authority_final_manifest.json
```

A green final manifest means both sides of the authority contract have evidence:
normal movement is persisted through the server boundary, while mutated movement
is rejected or failed before persistence.

## Step41 movement proposal validation

Capture movement proposals from the game. Checkpoints can stay enabled as comparison evidence, but the proposal path is the new authority-facing stream:

```bash
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-action-jsonl runtime/mmo_client_actions_step41_movement_proposal.jsonl \
  -mmo-action-session-key local-dev-PC_HERO_STEP41_MOVE_PROPOSAL \
  -mmo-action-queue-capacity 8192 \
  -mmo-action-movement-proposal-interval-ms 250 \
  -mmo-action-movement-proposal-min-distance 25 \
  -mmo-action-movement-proposal-min-yaw-deg 10 \
  -mmo-action-checkpoint-interval-ms 1000 \
  -mmo-action-checkpoint-min-distance 75 \
  -mmo-action-checkpoint-min-yaw-deg 15 \
  -mmo-action-checkpoint-force-interval-ms 5000
```

Validate proposal stream and emit accepted checkpoint rows:

```bash
python3 tools/check_mmo_step41_movement_proposal_jsonl.py \
  --jsonl runtime/mmo_client_actions_step41_movement_proposal.jsonl \
  --session-key local-dev-PC_HERO_STEP41_MOVE_PROPOSAL \
  --output runtime/mmo_step41_movement_proposal_check.json \
  --accepted-jsonl runtime/mmo_step41_movement_proposals.accepted.jsonl \
  --rejected-jsonl runtime/mmo_step41_movement_proposals.rejected.jsonl \
  --accepted-checkpoint-jsonl runtime/mmo_step41_movement_proposals.accepted_checkpoints.jsonl \
  --min-accepted 2 \
  --max-rejected 0 \
  --require-position-change \
  --max-step-distance 2500 \
  --max-horizontal-speed 2500 \
  --max-vertical-speed 2500 \
  --max-vertical-delta 1600
```

Run accepted proposals through the existing checkpoint procedure chain:

```bash
python3 tools/run_mmo_step41_movement_proposal_e2e.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --proposal-jsonl runtime/mmo_client_actions_step41_movement_proposal.jsonl \
  --source-session-key local-dev-PC_HERO_STEP41_MOVE_PROPOSAL \
  --session-key local-dev-PC_HERO_STEP41_MOVE_PROPOSAL_E2E \
  --output runtime/mmo_step41_movement_proposal_run.json \
  --proposal-check-output runtime/mmo_step41_movement_proposal_check.json \
  --accepted-proposal-jsonl runtime/mmo_step41_movement_proposals.accepted.jsonl \
  --rejected-proposal-jsonl runtime/mmo_step41_movement_proposals.rejected.jsonl \
  --accepted-checkpoint-jsonl runtime/mmo_step41_movement_proposals.accepted_checkpoints.jsonl \
  --e2e-output runtime/mmo_step41_movement_proposal_e2e.json \
  --mysql-check-output runtime/mmo_step41_movement_proposal_mysql_e2e.json \
  --manifest-output runtime/mmo_step41_movement_proposal_manifest.json \
  --min-accepted 2 \
  --require-position-change \
  --reset-matching-failed \
  --max-replay-rows 20 \
  --coalesce-min-distance 75 \
  --coalesce-force-tick-delta 5000
```

Expected interpretation:
- `movement_proposal` JSONL proves the client sends a proposal/intent stream, not only final checkpoint consequences.
- The Step41 checker is the server authority gate prototype.
- Only accepted proposals are converted to `character_checkpoint` and persisted through MySQL.
- A rejected proposal is valid evidence only when it is rejected before replay/DB dispatch.

## Step42 fall-aware movement proposal validation

After applying Step42 and rebuilding, capture proposals while walking and while
falling from a safe test cliff:

```bash
./build/opengothic/Gothic2Notr \
  -g "/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/" \
  -g2 \
  -mmo-action-jsonl runtime/mmo_client_actions_step42_movement_proposal.jsonl \
  -mmo-action-session-key local-dev-PC_HERO_STEP42_MOVE_PROPOSAL \
  -mmo-action-queue-capacity 8192 \
  -mmo-action-movement-proposal-interval-ms 250 \
  -mmo-action-movement-proposal-min-distance 25 \
  -mmo-action-movement-proposal-min-yaw-deg 10 \
  -mmo-action-checkpoint-interval-ms 1000 \
  -mmo-action-checkpoint-min-distance 75 \
  -mmo-action-checkpoint-min-yaw-deg 15 \
  -mmo-action-checkpoint-force-interval-ms 5000
```

Positive fall-aware check:

```bash
python3 tools/check_mmo_step41_movement_proposal_jsonl.py \
  --jsonl runtime/mmo_client_actions_step42_movement_proposal.jsonl \
  --session-key local-dev-PC_HERO_STEP42_MOVE_PROPOSAL \
  --output runtime/mmo_step42_movement_proposal_check.json \
  --accepted-jsonl runtime/mmo_step42_movement_proposals.accepted.jsonl \
  --rejected-jsonl runtime/mmo_step42_movement_proposals.rejected.jsonl \
  --accepted-checkpoint-jsonl runtime/mmo_step42_movement_proposals.accepted_checkpoints.jsonl \
  --min-accepted 2 \
  --max-rejected 0 \
  --require-position-change \
  --max-step-distance 2500 \
  --max-horizontal-speed 2500 \
  --max-vertical-speed 2500 \
  --max-vertical-delta 1600 \
  --max-fall-speed 9000 \
  --max-fall-delta 12000 \
  --require-motion-state-for-large-fall
```

Full E2E still converts only accepted proposals into checkpoint rows:

```bash
python3 tools/run_mmo_step41_movement_proposal_e2e.py \
  --url "mysql://gothic:gothic_dev_password@localhost:3306/gothic_mmo" \
  --proposal-jsonl runtime/mmo_client_actions_step42_movement_proposal.jsonl \
  --source-session-key local-dev-PC_HERO_STEP42_MOVE_PROPOSAL \
  --session-key local-dev-PC_HERO_STEP42_MOVE_PROPOSAL_E2E \
  --output runtime/mmo_step42_movement_proposal_run.json \
  --proposal-check-output runtime/mmo_step42_movement_proposal_check.json \
  --accepted-proposal-jsonl runtime/mmo_step42_movement_proposals.accepted.jsonl \
  --rejected-proposal-jsonl runtime/mmo_step42_movement_proposals.rejected.jsonl \
  --accepted-checkpoint-jsonl runtime/mmo_step42_movement_proposals.accepted_checkpoints.jsonl \
  --e2e-output runtime/mmo_step42_movement_proposal_e2e.json \
  --mysql-check-output runtime/mmo_step42_movement_proposal_mysql_e2e.json \
  --manifest-output runtime/mmo_step42_movement_proposal_manifest.json \
  --min-accepted 2 \
  --require-position-change \
  --reset-matching-failed \
  --max-replay-rows 20 \
  --coalesce-min-distance 75 \
  --coalesce-force-tick-delta 5000 \
  --require-motion-state-for-large-fall
```

Negative suite:

```bash
python3 tools/run_mmo_step41_movement_proposal_negative_suite.py \
  --jsonl runtime/mmo_client_actions_step42_movement_proposal.jsonl \
  --session-key local-dev-PC_HERO_STEP42_MOVE_PROPOSAL \
  --output-dir runtime/step42_movement_proposal_negative_suite \
  --output runtime/mmo_step42_movement_proposal_negative_suite.json \
  --min-accepted 2 \
  --max-step-distance 2500 \
  --max-horizontal-speed 2500 \
  --max-vertical-speed 2500 \
  --max-vertical-delta 1600 \
  --max-fall-speed 9000 \
  --max-fall-delta 12000 \
  --require-motion-state-for-large-fall
```

Final manifest:

```bash
python3 tools/build_mmo_step42_movement_proposal_final_manifest.py \
  --source-session-key local-dev-PC_HERO_STEP42_MOVE_PROPOSAL \
  --positive-manifest runtime/mmo_step42_movement_proposal_manifest.json \
  --negative-suite runtime/mmo_step42_movement_proposal_negative_suite.json \
  --output runtime/mmo_step42_movement_proposal_final_manifest.json
```

Expected interpretation:
- normal walking and plausible cliff fall are accepted;
- upward fly, teleport, impossible marked fall, invalid position and tick reversal are rejected;
- after the first rejected hostile segment, continuity rejects dependent stale follow-up proposals until a future live server sends correction/resync.
