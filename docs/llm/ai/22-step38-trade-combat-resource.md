# 22 Step38 trade/combat/resource slice

Step37 proved a real script/progression slice end-to-end. Step38 begins the next production-relevant surface: player trade, ammunition/resource consumption, damage and NPC lifecycle.

## C++ hooks

Added post-success player-related hooks in `game/world/objects/npc.cpp`:

- `Npc::buyItem`: after inventory transfer and gold mutation, emits `trade_buy_from_npc`.
- `Npc::sellItem`: after inventory transfer and gold mutation, emits `trade_sell_to_npc`.
- `Npc::shootBow`: after bullet creation and ammunition deletion, emits `consume_item` with reason `ranged_ammunition`.
- `Npc::changeAttribute`: after attribute clamp and health checks, emits:
  - `consume_mana` for player mana decreases;
  - `apply_character_damage` for player HP decreases;
  - `apply_world_entity_damage` for non-player HP decreases caused by the player.
- `Npc::onNoHealth`: after death/unconscious transition, emits `mark_npc_dead` for non-player death caused by the player.

Disabled mode remains a cheap branch through the semantic action sink guard. Enabled mode snapshots compact immutable payloads and queues them; it does not call MySQL or block the game thread.

## Server-boundary support

Updated `tools/run_mmo_action_receiver.py` to normalize Step38 payloads into outbox resolver fields.

Updated `tools/run_mmo_resolved_action_worker.py` with conservative dispatch for:

- `consume_mana` -> `mmo_consume_character_mana`
- `consume_item` -> `mmo_consume_character_item`
- `apply_character_damage` -> `mmo_apply_character_damage`
- `apply_world_entity_damage` -> `mmo_apply_world_entity_damage`
- `mark_npc_dead` -> `mmo_mark_npc_dead`
- `trade_sell_to_npc` -> `mmo_trade_sell_to_npc`
- `trade_buy_from_npc` -> `mmo_trade_buy_from_npc`

Trade buy resolution requires a unique NPC/world inventory item. Trade sell and consume item use character inventory resolution by item symbol/persistent-id. Ambiguous or missing rows fail instead of inventing authority.

## Validation tools

New tools:

- `tools/check_mmo_step38_trade_combat_jsonl.py`: validates local JSONL shape and action coverage.
- `tools/check_mmo_step38_trade_combat_mysql.py`: checks outbox/journal evidence for Step38 kinds.
- `tools/run_mmo_step38_trade_combat_e2e.py`: replays captured Step38 JSONL through receiver -> outbox -> worker -> MySQL checker.

## Known limitations

This does not yet complete full Step38 parity. It proves producer and server-boundary plumbing. Full parity still needs controlled fixtures or real synchronized runtime/MySQL projections for trade vendors, combat targets and restart/load comparison.
