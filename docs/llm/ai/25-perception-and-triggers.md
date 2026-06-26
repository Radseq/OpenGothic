# Perception And Triggers

Files: `game/game/perceptionmsg.h`, `game/world/world.cpp`, `game/world/worldobjects.*`, `game/world/triggers/*`.

- `PerceptionMsg` carries transient actor, target, victim, item, and position references.
- `World::sendPassivePerc` and `sendImmediatePerc` dispatch gameplay perception through `WorldObjects`.
- `World::triggerEvent`, `execTriggerEvent`, and `triggerOnStart` delegate scripted world triggers to `WorldObjects`.

Do not persist pointer-based perception messages or trigger queues. Persist only their durable consequences: triggered world state, quest/dialog/global changes, item changes, NPC lifecycle changes, and explicitly idempotent one-shot events.

