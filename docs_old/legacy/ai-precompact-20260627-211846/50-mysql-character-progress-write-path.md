# MySQL Character Progress Write Path

Migration `008_character_progress_write_path.sql` adds a server-owned write path for script, quest,
dialog and progression mutations.

## New table

`character_progress_audit` records accepted mutations for:

- script integer state, such as one-shot Gothic flags and bookstand/read flags;
- quest state updates;
- known/consumed dialog state;
- progression adjustments, such as experience and learning-point reward deltas.

The audit table is not the gameplay source of truth. The source remains:

```text
world_event_journal + deterministic projection updates
```

## Procedures

`mmo_set_character_script_int(...)` writes one integer script value into
`character_script_state` and appends `character_script_int_set`.

`mmo_update_character_quest(...)` writes one row in `character_quests` and appends
`character_quest_updated`.

`mmo_set_character_known_dialog(...)` writes one row in `character_known_dialogs` and appends
`character_dialog_known_set`.

`mmo_adjust_character_progression(...)` updates `character_stats.experience` and
`character_stats.learning_points` with signed deltas and appends `character_progression_adjusted`.
It rejects negative final values.

`mmo_apply_character_experience_reward(...)` is a positive-reward wrapper for normal XP gains.

## Idempotency rule

Every procedure requires an idempotency key. A retry with the same `world_instance_id + idempotency_key`
returns the original event and does not apply the projection again.

## Bookstand/read-state relevance

A bookstand-like one-shot can be represented as:

```text
character_script_int_set: script flag from 0/null -> 1
character_progression_adjusted: +XP reward
```

That gives the server an explicit durable event for both the one-shot flag and the reward, instead of
trying to infer the effect later from SQLite/world diffs.
