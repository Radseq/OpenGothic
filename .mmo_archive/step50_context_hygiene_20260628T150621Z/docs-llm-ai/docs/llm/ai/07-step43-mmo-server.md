# 07 Step43 MMO Server

Step43 creates the first real MMO server process boundary for the current Python
prototype phase. It intentionally does not put MySQL inside OpenGothic. The game
client still emits semantic envelopes asynchronously; the separate server process
validates, rejects or converts them, and optionally writes accepted mutations to
`mmo_server_action_outbox`.

## Why this step exists

After Step42, movement evidence is good enough to stop refining offline nuance.
A real cliff fall can be accepted when the proposal is marked with plausible
fall/airborne state, while horizontal teleport, upward fly, invalid numbers,
time reversal and impossible fall remain rejected. The missing piece is no longer
another checker; it is a live process that owns the authority decision before DB
persistence.

## New live pipeline

```text
OpenGothic client
  -> UDP semantic envelope
  -> server/mmo_server.py
  -> validate envelope/session/idempotency
  -> if movement_proposal: stateful fall-aware authority
  -> if accepted movement: generate character_checkpoint
  -> accepted JSONL / checkpoint JSONL / rejected JSONL
  -> optional mmo_server_action_outbox enqueue
  -> existing resolved worker -> MySQL procedures -> journal/projections
```

## Movement ownership rule

`movement_proposal` is an intent/proposal, not a database mutation. The server is
the first owner that may turn it into a durable mutation. Therefore:

- never dispatch `movement_proposal` directly to MySQL;
- only enqueue a generated `character_checkpoint` after the live server authority
  accepts the proposal;
- write rejected proposals to reject JSONL with `authority_reject_reason`;
- keep continuity state per session/world/character/actor stream so a rejected
  hostile segment does not let later stale proposals continue from a position
  the server never accepted.

## Files

- `server/mmo_server.py`: direct executable wrapper.
- `server/mmo/server.py`: UDP server loop, signal handling, artifact writing,
  optional MySQL enqueue.
- `server/mmo/actions.py`: envelope validation, stable DB payload mapping,
  movement proposal -> checkpoint conversion.
- `server/mmo/authority.py`: live Step43 movement validator.
- `server/mmo/db.py`: MySQL CLI bridge for `mmo_login_character` and
  `mmo_enqueue_server_action`.
- `tools/run_mmo_server.py`: project-root wrapper.
- `tools/run_mmo_step43_server_smoke.py`: no-MySQL smoke test.
- `tools/check_mmo_step43_server_live.py`: artifact checker.

## Smoke test

```bash
python3 tools/run_mmo_step43_server_smoke.py \
  --output-dir runtime/step43_server_smoke \
  --session-key local-dev-PC_HERO_STEP43_SERVER_SMOKE
```

The smoke sends four packets to the live server:

1. legal walking movement proposal;
2. legal marked fall proposal;
3. hostile horizontal teleport proposal;
4. direct checkpoint passthrough row.

Expected result:

```text
accepted movement_proposal >= 2
character_checkpoint rows >= 2
rejected rows >= 1
required reject reason: horizontal_speed_too_large
fall_segments >= 1
status=passed
```

## Live server without DB

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

Run OpenGothic with the same UDP/session flags and movement proposal capture
enabled. The server will keep running until interrupted unless `--max-packets` is
specified.

## Live server with DB outbox enqueue

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

Then apply accepted outbox rows through the existing resolved worker:

```bash
python3 tools/run_mmo_resolved_action_worker.py \
  --mysql-url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP43 \
  --max-actions 100
```

## Validation

```bash
python3 tools/check_mmo_step43_server_live.py \
  --summary runtime/mmo_server_step43_summary.json \
  --accepted-jsonl runtime/mmo_server_actions_step43.jsonl \
  --rejected-jsonl runtime/mmo_server_rejects_step43.jsonl \
  --checkpoint-jsonl runtime/mmo_server_checkpoints_step43.jsonl \
  --output runtime/mmo_step43_server_live_check.json \
  --session-key local-dev-PC_HERO_STEP43 \
  --min-accepted-movement-proposals 1 \
  --min-checkpoints 1 \
  --require-fall-segment
```

## Current limitation

This is a production-shaped server boundary, not the final production MMO shard.
It is still Python and UDP. It proves process ownership, authority gating,
server-created mutations and DB enqueue separation. Later hot paths should move
to the final server runtime, but the architectural contract must stay the same:
client proposals are not durable truth; the server decides and emits accepted
mutations.
