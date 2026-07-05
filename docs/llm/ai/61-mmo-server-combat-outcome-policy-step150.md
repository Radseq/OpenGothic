# MMO server combat outcome policy - step 150

## Durable rule added

When moving gameplay logic from the client to the server, inspect the client and
Gothic script behavior first. Do not invent a parallel interpretation.

The server implementation should preserve the same semantics, adjusted only for
server authority, validation, synchronization and anti-cheat needs.

## Client behavior observed

Relevant client facts:

- `GameScript::isDead` checks `ZS_Dead`.
- `GameScript::isUnconscious` checks `ZS_Unconscious`.
- MMO observation maps NPC action/fight state:
  - `dead` when `npc.isDead()`,
  - `down` when `npc.isUnconscious()` or `npc.isDown()`,
  - `combat` when attacking/engaged.
- NPC inventory loot observation is allowed only when the source NPC is dead or
  unconscious.
- Damage observation reports `fatal=true` when hitpoints reach zero.
- The lifecycle observation previously emitted only dead NPCs; it now emits dead
  or unconscious NPCs.

## What changed

Added a header-only server module:

- `server/cpp/mmo_server_combat_outcome_authority.h`

It decides:

- `Combat`
- `Down`
- `Dead`

from:

- fatal damage,
- observed dead state,
- observed unconscious/down state,
- target-is-player,
- explicit lethal/non-lethal intent flags,
- explicit kill permission flags.

## Current policy

The policy is intentionally conservative:

- observed `dead` -> `Dead`,
- observed `unconscious`/`down` -> `Down`,
- non-fatal damage -> `Combat`,
- fatal damage against player -> `Down`,
- fatal damage with `non_lethal`/`knockout_intent` -> `Down`,
- fatal damage with `lethal`/`kill_intent`/`allow_kill`/`source_can_kill` -> `Dead`,
- fatal damage without explicit kill permission -> `Down`.

This gives the server a place to model Gothic's important distinction:

- some attackers can kill,
- some attackers should only knock the opponent unconscious and allow looting.

## Integration

`ApplyWorldEntityDamage` now uses `CombatOutcome::decide`.

Important detail: when server outcome is `Down`, the direct DB damage record is
not sent as fatal. The current DB model mostly understands dead/alive, and it is
temporary. Until the DB is replaced, `Down` is a runtime authority state, not a
final persistence model.

`MarkNpcDead` now accepts payloads with:

- `dead=true` -> `Dead` and DB mark-dead,
- `unconscious=true` or `down=true` with `dead=false` -> runtime `Down`, no
  mark-dead DB call.

`game/game/mmosemantichooks.cpp` now emits NPC lifecycle observation when an NPC
is dead or unconscious, not only when dead.

## Next good step

Build a server-side combat intent/threat module that feeds this outcome policy
with Gothic-derived rules:

- attacker species/guild can kill or only knock out,
- scripted state/AI routine can request lethal or non-lethal combat,
- player crime can escalate witnesses/guards differently,
- monsters and humans can have different finish rules,
- unconscious NPC loot should be explicitly represented in the future DB model.
