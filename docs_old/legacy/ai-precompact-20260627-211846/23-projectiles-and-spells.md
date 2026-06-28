# Projectiles And Spells

Files: `game/world/bullet.h`, `game/world/bullet.cpp`, `game/world/objects/npc.cpp`, `game/world/world.cpp`.

- `Npc::shootBow` creates a world bullet and consumes ammunition immediately.
- `beginCastSpell` and `tickCast` manage casting; `commitSpell` creates spell effects/projectiles when the cast commits.
- `World::shootBullet` and `World::shootSpell` create the projectile.
- `Bullet::onCollide(Npc&)` applies damage unless friendly-fire rules prevent it.

Projectiles, cast animations, and collision callbacks are transient. Persist the resulting resource consumption, damage, death, and durable script effects as ordered events.

