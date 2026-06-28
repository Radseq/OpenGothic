# C++ Semantic Event Scaffold

Files:

- `game/game/mmosemanticevents.h`
- `game/game/mmosemanticevents.cpp`

The scaffold defines stable semantic action names and event-type mappings for the DB write paths.

It deliberately has no MySQL dependency. The next production step is to call this contract at mutation boundaries and route envelopes to either a server RPC worker or a local CLI/dev adapter.

Preferred hook boundaries remain:

- `World::takeItem`, `World::removeItem`;
- `Inventory::transfer`, `Inventory::equip`, `Inventory::unequip`;
- `Npc::sellItem`, `Npc::buyItem`;
- `Npc::commitSpell`, projectile damage result paths;
- `Interactive` committed container/door/lock/state transitions;
- `GameScript` and `GameSession` dialog/quest/script progress hooks.
