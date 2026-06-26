# Physics And Collision

Files: `game/physics/collisionworld.*`, `game/physics/dynamicworld.*`, `game/world/objects/npc.*`, `game/world/bullet.*`.

- `CollisionWorld` wraps Bullet rigid bodies and converts engine centimeters to Bullet meters.
- `DynamicWorld` owns landscape/object collision, NPC ghost bodies, ray queries, water/camera tests, bullets, and trigger bounding boxes.
- `Npc::tryMove` and `Npc::tryTranslate` consume `DynamicWorld::CollisionTest` results for local movement.
- `Bullet` receives physics callbacks and converts a collision into gameplay damage/effects.

Bullet bodies, ghost bodies, contact manifolds, and ray callbacks are process-local. Persist authoritative transforms and resulting state changes; future MMO movement/combat validation must run equivalent collision rules on the server.

