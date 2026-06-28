# Step41 movement proposal / input-intent harness

Step41 starts the transition from Step39/40 checkpoint authority toward real server-owned movement.

Step39/40 persisted bounded character checkpoints. That is useful for reconnect and projection evidence, but it still represents a consequence observed by the client. Step41 adds a separate `movement_proposal` stream:

```text
from_tick/from_pos -> to_tick/to_pos
```

The OpenGothic process still does not call MySQL. It emits immutable JSONL/UDP envelopes through the existing semantic action sink. The server-side tooling validates proposals and only then converts accepted proposals into checkpoint rows for the existing MySQL procedure chain.

## C++ flags

```text
-mmo-action-movement-proposal-interval-ms <0|>=50>
-mmo-action-movement-proposal-min-distance <world-units>
-mmo-action-movement-proposal-min-yaw-deg <degrees>
```

`0` disables proposal capture. The game records an initial baseline internally and emits proposals only after the next accepted cadence/distance/yaw delta.

## Envelope kind

```text
action_kind = movement_proposal
event_type  = movement_proposal_submitted
event_class = movement
procedure   = server_validate_movement_proposal
```

The `procedure` name is intentionally not a MySQL stored procedure. It documents the server authority boundary: proposals must be validated before any durable checkpoint mutation.

## Server validation prototype

`tools/check_mmo_step41_movement_proposal_jsonl.py` validates:
- required payload shape;
- unique idempotency keys;
- monotonic ticks;
- positive delta time;
- finite positions;
- max step distance;
- max horizontal speed;
- max vertical speed and vertical delta;
- optional world bounds.

Accepted proposals can be converted into `character_checkpoint` JSONL rows with `reason=server_accepted_movement_proposal`. Those rows can be replayed through the existing Step39 E2E chain.

## Meaning

Step41 does not implement final MMO movement replication. It introduces the correct ownership split:

```text
client proposal -> server authority check -> durable accepted checkpoint
```

The next step after green Step41 is a live movement proposal transport with server ticks, correction/snapback responses and interest-managed replication snapshots.
