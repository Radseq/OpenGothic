# 03 Engine Surfaces

Session/lifecycle:
- `game/game/gamesession.*`: owns runtime SQLite bridge; constructs world/script/player first, then DB. Ticks world/script then DB tick last. Destructor flushes SQLite. Dialog hook surface exists here.
- `game/game/gamescript.*`: owns Daedalus VM, quests, dialogs, global vars, guild attitude, gold id/name. Use restore APIs; do not write VM internals directly.
- `game/mainwindow.*`, renderer, UI, camera, audio are presentation/input layers. They should send intents or call gameplay APIs, never write durable MMO rows.

World/entity:
- `game/world/world.*`: facade over `WorldObjects`, navigation, physics, triggers, rendering. Useful boundaries: `addNpc`, `removeNpc`, `addItem`, `takeItem`, `removeItem`, `shootBullet`, `shootSpell`.
- `game/world/worldobjects.*`: owns NPC arrays, item arrays, interactives, trigger queues, routines, persistent id allocation. `npcArr` may sort during tick; never use array index as identity. `takeItem` removes item from world array, disables physics, calls `onItemRemoved`.
- `game/world/objects/item.*`: `clsId()` is Daedalus item template; `persistentId()` is durable source identity for world item.
- `game/world/objects/interactive.*`: `getId()` stable VOB identity; `setMobState` can animate/change state; `inventory()` owns container contents; `restorePersistentState` is approved restore path.

NPC/inventory/combat:
- `game/world/objects/npc.*`: `persistentId()` stable NPC identity. `PersistentState` covers attributes/protections/talents/missions/AI vars/guild/progression/attitudes/death. `restorePersistentInventory` uses Inventory APIs.
- `Npc::buyItem` and `Npc::sellItem` wrap trade, gold and inventory transfer. Gold item is special and must not be transferred as normal trade item.
- `Npc::dropItem` creates dynamic world item and deletes inventory count after successful animation/setup.
- `Npc::shootBow` validates ammunition, starts attack, creates bullet and consumes ammunition.
- `Npc::commitSpell` creates spell/projectile/effect and may invoke script spell effect.
- `Npc::onNoHealth` performs death/unconscious transition; durable result is health/death/resources/script effects, not animation internals.
- `game/game/inventory.*`: `Inventory::transfer` is shared primitive for inventory/container transfers; `equip/unequip/use` mutate equipment and may affect derived stats; consumables call Daedalus `on_state`; torches may consume one item after successful toggle.

Transient exclusions:
- Do not persist live AI/path/fight queues, raw pointers, focus, perception messages, trigger queues, Bullet bodies/contact callbacks, GPU/render objects, animation pose/frame, particles, audio playback, camera/input state.
- Persist only durable consequences: transform checkpoints, state flags, inventory/resource deltas, quest/dialog/global changes, NPC lifecycle, container/door state, damage/death, one-shot idempotent events.
