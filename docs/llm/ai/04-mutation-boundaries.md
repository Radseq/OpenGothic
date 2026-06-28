# 04 Mutation Boundaries

Golden rule: emit/submit exactly one semantic action after a mutation succeeds. Do not emit before success. Do not infer from later snapshot diff except for validation. Game thread must not block on MySQL/network.

Current semantic boundaries:
- World item pickup/removal: `World::takeItem`, `World::removeItem`, `WorldObjects::takeItem/removeItem`.
- Inventory/container transfer: `Inventory::transfer` after item count/ownership is changed; identify source/target owner and item symbol/instance/amount.
- Equipment: `Inventory::equip`, `Inventory::unequip`, `Inventory::use` when routed to equipment slots.
- Item use/consumption: `Inventory::use`, `Npc::useItem`; consumables can call script `on_state`, so capture resulting state/XP/script global via script/progression hooks too.
- Trade: `Npc::buyItem`, `Npc::sellItem`; includes item transfer and gold spend/grant. Treat as trade semantic action, not independent guessed wallet/inventory diff.
- Drop item: `Npc::dropItem`; world item spawn + inventory decrement after success.
- Bow/crossbow: `Npc::shootBow`; consumes ammunition and creates projectile. Persist ammo delta and later damage/death result.
- Spells: `Npc::beginCastSpell/tickCast/commitSpell`, `World::shootSpell`; persist mana/resource consumption, spell committed, damage/death/script effects, not particles.
- Combat/death: damage application sites and `Npc::onNoHealth`; persist hp/mana/death/unconscious plus durable quest/script consequences.
- Interactive/container/door/lock: `Interactive::setMobState`, attach/detach/inventory commit points; persist state/lock/cracked/container inventory deltas after success.
- Dialog: `GameSession::updateDialog` -> `dialog_choice_updated`; `GameSession::dialogExec` before `GameScript::exec` -> `dialog_choice_executed`; after script exec, capture known dialog/quest/script deltas.
- Script progress: `GameScript::exec`, quest APIs, `saveVar/loadVar` surfaces, chapter `KAPITEL` changes, XP/LP/stat mutations.

Action envelope for client->server boundary:
```text
version, action_kind, local_sequence, idempotency_key,
account/session/character key, world key/instance, actor key,
target key(s), item/template keys, amount/slot, client tick/time,
minimal payload JSON, trace/source location, optional debug snapshot hash
```

Idempotency key format should be deterministic per accepted local action, e.g. `<session>:<kind>:<actor>:<target>:<local_seq>`. Server may replace/augment with authoritative sequence after validation.

First implementation rule:
- Add a no-blocking `MmoSemanticActionSink` interface and bounded queue/adapter.
- In singleplayer/dev, actions may be written to local JSONL or local outbox transport. Production path must be `client -> MMO server`, never direct client MySQL.
- Hooks should be guarded and cheap when MMO bridge is disabled.

## Step 32/34 hook foundation implemented

Added C++ semantic action foundation:
- `game/game/mmosemanticevents.*`: constexpr action registry plus JSONL serialization helpers.
- `game/game/mmosemanticactionsink.*`: disabled-by-default sink, session key, sequence generator, bounded queued JSONL dev transport.
- `game/game/mmosemantichooks.*`: post-success hook helper layer. Hook code first checks `isSemanticActionCaptureEnabled()` so disabled mode stays cheap.
- `game/commandline.*`: dev flags `-mmo-action-jsonl`, `-mmo-action-session-key`, `-mmo-action-queue-capacity`, `-mmo-action-strict-overflow`.

First hook sites:
- `Npc::takeItem`: emits `pickup_world_item` after the world item is removed and the actor inventory receives the item.
- `WorldObjects::removeItem`: emits `remove_world_item` for non-pickup world item removal.
- `Inventory::transfer`: emits `transfer_character_item` after the source and destination inventories are changed.
- `Inventory::setSlot`: emits `equip_character_item` and `unequip_character_item` after equipment state/stat/script callbacks are applied.

Limitations:
- The JSONL sink is dev evidence, not production networking.
- The transfer hook is intentionally generic and may need owner-context refinement when server transport is added.
- Production flow remains `client -> MMO server -> MySQL`; do not wire gameplay thread directly to MySQL.
