# MMO server NPC activity registry - step 148

## What changed

Added a server-side NPC activity registry:

- `server/cpp/mmo_server_npc_activity_authority.h`
- `server/cpp/mmo_server_npc_activity_authority.cpp`

The registry tracks what an NPC is currently doing at runtime. It is intentionally
small and in-memory for now, because the current database model is temporary and
will be replaced later.

## Why

NPC actions must become server-authoritative. A Gothic MMO server cannot let each
client independently decide that two NPCs are talking, sleeping, fighting or using
a mob object. If a player enters range after an activity has already started, the
server must know the current state and send the player the activity snapshot with
elapsed/remaining timing.

This step does not finish range-based broadcasting yet. It creates the authority
primitive that later broadcast/snapshot code can read from.

## Runtime rules

`NpcActivity::Registry` stores one active activity per actor key.

Known kinds:

- `Idle`
- `Routine`
- `Moving`
- `Talking`
- `Sleeping`
- `UsingMob`
- `Combat`
- `Down`
- `Dead`

Exclusive kinds are:

- `Talking`
- `Sleeping`
- `UsingMob`
- `Combat`
- `Down`
- `Dead`

An exclusive activity cannot replace another exclusive activity unless:

- it is the same kind and same sync group,
- the previous activity timed out,
- the new activity is `Combat`, `Down` or `Dead`,
- the current activity is `Dead` and the next activity is also `Dead`.

This is deliberately conservative. It prevents obvious nonsense such as:

- an NPC sleeping and talking at the same time,
- two different conversations claiming the same NPC,
- a mob-use action starting while the NPC is already locked in a conversation.

## Dialog locking

`applyDialogLock` locks both the speaker and listener into `Talking` using the
same `conversationKey` as `syncGroup`.

The check is transactional in spirit: it validates both participants first, then
applies the lock. If either participant is busy with another exclusive activity,
the dialog line is rejected with a diagnostic reason such as:

- `npc_activity_dialog_speaker_busy`
- `npc_activity_dialog_listener_busy`

This supports the architecture decision that the server owns NPC conversations.
The current client `RecordNpcDialogLine` hook is only an observation bridge until
the full server AI/dialog scheduler exists.

## Integration

`mmo_udp_server.cpp` now owns:

```cpp
Mmo::Server::NpcActivity::Registry gNpcActivityRegistry;
```

`mmo_udp_server_direct_world_state_apply.inl` now:

- applies `RecordNpcActionState` into `gNpcActivityRegistry`,
- expires old NPC activity states before dialog line processing,
- applies a dialog lock before accepting `RecordNpcDialogLine`,
- rejects conflicting activity/dialog events before they reach outbox fallback.

`server/cpp/CMakeLists.txt` builds the new module.

## Next good step

Add a server snapshot/broadcast path for active NPC activities and active
conversations. A newly entering player should receive:

- actor key,
- activity kind,
- target/listener key if any,
- sync group/conversation key,
- elapsed server time,
- expected remaining duration.

That snapshot should be distance/interest filtered, not global spam.
