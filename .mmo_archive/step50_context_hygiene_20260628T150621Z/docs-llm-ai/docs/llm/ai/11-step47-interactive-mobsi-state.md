# 11 Step47 Interactive / Mobsi State

Step47 closes a live gameplay gap found in Xardas' tower: using the fireplace/mobsi that opens the hidden barred room did not produce a server-side semantic action.

## Problem

The project contract already listed `Interactive::setMobState`, doors, locks and container/interactives as durable mutation boundaries, but the live server path did not yet have a C++ producer for accepted mobsi use/state changes. Script/global diffs are not enough: some Gothic interactives trigger hidden doors, movers or room access without a clean quest/script/global delta.

## C++ producer additions

New semantic action kind:

```text
use_interactive
```

Existing action now has a real producer:

```text
update_interactive_state
```

Hook sites:

```text
Interactive::attach(...)
  -> use_interactive after use is accepted
  -> update_interactive_state if lock/cracked state changed during use condition checks

Interactive::setState(...)
  -> update_interactive_state when state/locked/cracked changed
```

The payload uses the same stable mobsi key shape as runtime SQLite/MySQL import:

```text
mobsi:<world>:<slot_id>:<vob_id>:<focus_name>
```

Payload includes:

```text
interactive_key / interactive_entity_key
action actor when present
slot_id, vob_id, tag, focus_name, display_name, scheme
state_before, state_after, state_count, state_mask
locked_before/after, cracked_before/after
container/door/ladder flags
position and world/client_tick
```

## Server / worker additions

`server/mmo/actions.py` normalizes:

```text
use_interactive
update_interactive_state
```

`run_mmo_resolved_action_worker.py` behavior:

```text
use_interactive -> capture-only applied no-op
update_interactive_state -> real mmo_update_interactive_state(...)
```

The update resolver first tries the exact `mobsi:<world>:<slot>:<vob>:<focus>` entity key and then falls back to vob/slot identity inside `world_entity_state.state_json`. Ambiguous matches fail instead of mutating the wrong interactive.

## Acceptance

Run a live session near Xardas' tower, use the fireplace that opens the hidden barred room, then require the interactive domain:

```text
python3 tools/run_mmo_step47_interactive_followup.py \
  --url "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo" \
  --session-key local-dev-PC_HERO_STEP47 \
  --max-actions 1000 \
  --require-domain interactive
```

Expected server evidence:

```text
last=use_interactive decision=accepted
last=update_interactive_state decision=accepted
```

`use_interactive` is an accepted client/server evidence event. `update_interactive_state` is the durable projection mutation path.

## Limits

`use_interactive` is intentionally capture-only until a canonical server-side interactive-intent procedure exists. The durable state mutation is `update_interactive_state` via existing `mmo_update_interactive_state(...)`.
