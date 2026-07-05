# MMO server NPC action priority - step 149

## What changed

NPC activity now has explicit server-side priority/preemption semantics.

The important files are:

- `server/cpp/mmo_server_npc_activity_authority.h`
- `server/cpp/mmo_server_npc_activity_authority.cpp`
- `server/cpp/mmo_server_conversation_authority.h`
- `server/cpp/mmo_server_conversation_authority.cpp`
- `server/cpp/mmo_udp_server.cpp`
- `server/cpp/mmo_udp_server_direct_world_state_apply.inl`
- `server/cpp/mmo_udp_server_direct_combat_apply.inl`
- `server/cpp/mmo_udp_server_direct_interactive_apply.inl`

## Priority model

The activity registry now ranks activity kinds:

- `Unknown`: 0
- `Idle`: 1
- `Routine`: 10
- `Moving`: 20
- `Talking`, `Sleeping`, `UsingMob`: 40
- `Alert`: 60
- `Combat`: 80
- `Down`: 90
- `Dead`: 100

Higher priority can preempt lower priority. Equal priority still needs the same
sync group unless the previous activity has timed out.

This means:

- combat can interrupt NPC dialog,
- weapon-ready alert can interrupt dialog/sleep/mob use,
- down/dead can interrupt everything,
- dialog cannot start over combat/dead,
- routine movement cannot interrupt dialog/combat.

## Conversation interruption

When an activity preempts `Talking`, the server cancels the interrupted
conversation by `conversationKey`.

`Conversation::Registry` now exposes:

```cpp
bool cancel(std::string_view conversationKey);
```

`mmo_udp_server.cpp` owns the helper:

```cpp
cancelInterruptedConversation(activity);
```

This is the first server-side mechanism for:

- "NPCs were talking",
- "a hostile NPC/player event happened",
- "the server interrupts that conversation for everyone",
- "late observers should not receive a stale active conversation".

## Integrated events

The following direct apply paths now update the runtime NPC activity registry:

- `RecordNpcFightState` -> `Combat`
- `RecordNpcActionState` -> parsed activity kind
- `ApplyWorldEntityDamage` -> `Combat` or `Dead` when fatal
- `MarkNpcDead` -> `Dead`
- `ReadyWeapon` -> `Alert`
- `HolsterWeapon` -> clears `Alert` only, without clearing `Combat` or `Dead`

`HolsterWeapon` intentionally does not call generic idle clear, because hiding a
weapon must not erase stronger state like combat or death.

## Architecture note

This is still a runtime authority layer, not the final AI scheduler. The final
server should eventually decide hostile perception, theft reactions, weapon
threat reactions, chase/attack tasks and dialog interruption itself.

For now, client-observed combat/weapon/damage events feed the registry so the
server has a consistent central state to broadcast and validate against.

## Next good step

Add an explicit NPC intent/task scheduler module that can produce activity
commands such as:

- `InterruptConversation`
- `DrawWeapon`
- `ChaseTarget`
- `AttackTarget`
- `CallGuards`
- `ReturnToRoutine`

That scheduler should read threat/crime/perception inputs and emit authoritative
NPC activity updates, instead of relying on client-observed action records.
