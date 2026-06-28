# MySQL Event Replay Contract Class Fix

Fix for Step 17 replay-contract validation.

## Problem

The smoke test after Steps 15..18 returned:

```text
[FAIL] event replay contract validation: <run>/1/0
```

The database procedures from migration `009_npc_lifecycle_write_path.sql` emit:

```text
npc_marked_dead    event_class = combat
npc_respawned      event_class = combat
```

The first Step 17 contract registry incorrectly registered both as `world_entity`. The event projection still targets `world_entity_state`, but replay validation must match the actually emitted `world_event_journal.event_class`.

## Fix

`017_event_replay_contract.sql` now registers both NPC lifecycle events with:

```text
event_class = combat
projection  = world_entity_state
```

This keeps old journal rows valid and makes the replay contract strict against what the write path actually emits.

## Apply

Re-run only migration 017, then the Steps 15..18 smoke test.
