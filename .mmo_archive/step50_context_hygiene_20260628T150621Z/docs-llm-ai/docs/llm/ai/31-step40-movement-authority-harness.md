# 31 Step40 Movement Authority Harness

Step39 v2 proved bounded checkpoint capture and persistence:

```text
OpenGothic checkpoint capture -> server boundary -> outbox -> worker -> mmo_checkpoint_character_state(...) -> journal/audit/projection
```

Step40 adds the first server-side authority gate for movement evidence. It does not yet add live netcode or collision-authoritative input handling. Instead, it creates a deterministic offline/dev harness that treats `character_checkpoint` rows as movement proposals and rejects impossible rows before persistence.

## Added tools

- `check_mmo_step40_movement_authority.py`
  - reads Step39 `character_checkpoint` JSONL;
  - filters by source session key;
  - validates monotonic ticks, segment distance, horizontal/vertical speed, vertical delta, optional world AABB bounds and optional allowed checkpoint reasons;
  - writes accepted proposals to one JSONL and rejected proposals to a separate annotated JSONL;
  - emits a JSON authority report with limits, decisions, bbox, distance/speed stats and rejection reasons.

- `run_mmo_step40_movement_authority_e2e.py`
  - runs the authority checker first;
  - replays only accepted rows through the existing Step39 E2E runner;
  - keeps MySQL access outside OpenGothic;
  - emits a run artifact plus manifest.

- `build_mmo_step40_movement_authority_manifest.py`
  - hashes source JSONL, authority report, accepted/rejected JSONL, E2E result and MySQL checker output;
  - requires authority, E2E and MySQL checker status to be `passed`.

## Why this matters

Checkpoint persistence alone is not movement authority. Without a server-side movement gate, a malicious client could submit a valid-looking checkpoint envelope with an impossible position. Step40 closes that evidence gap at the replay/validation layer:

```text
client checkpoint/proposal -> authority validation -> accepted JSONL -> receiver/outbox/worker/MySQL
                         \-> rejected JSONL evidence
```

This is still a bridge, not final production authority. The production server must eventually validate raw input/proposals against world collision, navigation, movement mode, animation state and shard interest. Step40 deliberately avoids inventing collision semantics before that server runtime exists.

## Passing criteria

A clean walking capture should show:

- authority status `passed`;
- accepted rows >= 2;
- rejected rows = 0;
- material position change;
- MySQL applied checkpoint rows >= replayed rows;
- matching journal/audit/projection evidence;
- Step40 manifest status `passed`.

Rejected rows are expected in hostile tests, but they must remain outside the accepted replay stream unless a specific test is deliberately checking rejection behavior with `--allow-rejections`.
