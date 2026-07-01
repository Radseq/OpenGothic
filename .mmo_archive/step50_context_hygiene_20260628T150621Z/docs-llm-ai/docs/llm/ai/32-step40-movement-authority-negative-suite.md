# 32 Step40 Movement Authority Negative Suite

Step40 v1 proved the normal path:

```text
clean Step39 checkpoint capture -> authority accepted JSONL -> receiver/outbox/worker -> MySQL checkpoint procedure
```

Step40 v2 proves the hostile path:

```text
mutated impossible checkpoint proposal -> authority rejection/fail-closed -> no MySQL replay
```

## Added tools

- `build_mmo_step40_movement_negative_corpus.py`
  - consumes a clean `character_checkpoint` capture;
  - emits deterministic hostile JSONL fixtures:
    - `teleport_xz.jsonl`,
    - `vertical_spike.jsonl`,
    - `time_reversal.jsonl`,
    - `outside_world_bounds.jsonl`,
    - `invalid_position.jsonl`;
  - optional `duplicate_idempotency.jsonl` for fatal shape/idempotency failure.

- `check_mmo_step40_movement_authority.py`
  - now supports negative assertions:
    - `--min-rejected`,
    - `--max-rejected`,
    - `--require-reject-reason`.
  - accepted movement metrics now only count accepted segments; rejected teleport
    segments no longer inflate accepted max speed/distance diagnostics.

- `run_mmo_step40_movement_negative_suite.py`
  - builds or loads the hostile corpus;
  - runs the authority checker for every scenario;
  - requires exact rejection reasons for the core hostile cases;
  - writes one compact suite artifact.

- `build_mmo_step40_movement_authority_final_manifest.py`
  - combines the positive Step40 E2E manifest and negative suite artifact;
  - status is green only when both normal-path persistence and hostile-path
    rejection evidence are green.

## Why this matters

A movement validator that only accepts normal captures is not enough for MMO
server authority. It must also prove that malformed or malicious client movement
cannot become durable state. This suite gives that proof without needing a live
networked movement runtime yet.

## Boundary

This remains a dev authority harness. It validates checkpoint/proposal streams,
not raw player input, animation state or collision. The next production step is
live movement intent/proposal handling inside an MMO server process, with
server-owned movement integration and replication snapshots.
