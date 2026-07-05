# MMO server AI client-parity audit - step 151

## Audit rule

When moving gameplay or AI logic to the server, the source of truth is the
current OpenGothic client plus Gothic scripts/content. The server may reshape the
logic for authority, replication, validation and anti-cheat, but it must not
invent a separate interpretation without first checking how the client does it.

This file audits the server-side "AI" work added so far through that rule.

## Current classification

Most current server AI work is not final AI simulation yet. It is a mixture of:

- observation ingestion from the client,
- runtime state registries,
- validation and conflict prevention,
- DB/outbox bridges,
- early server-authority scaffolding.

That is acceptable as an intermediate stage, but future work must not treat these
bridges as already proven Gothic-equivalent AI.

## Green: mostly client-derived or safe scaffolding

### NPC action/fight/down/dead naming

Client source checked:

- `game/game/mmosemantichooks.cpp`
- `game/game/gamescript.cpp`

The client maps:

- `npc.isDead()` -> `dead`
- `npc.isUnconscious()` or `npc.isDown()` -> `down`
- `npc.isTalk()` -> `talking`
- attack animation/state/victim/target -> `combat`

Server usage:

- `mmo_server_npc_activity_authority.cpp`
- `mmo_udp_server_direct_world_state_apply.inl`
- `mmo_udp_server_direct_combat_apply.inl`

This is acceptable as a temporary bridge because the server state names follow
the client observation names.

### Conversation snapshot timing

Client source checked:

- `GameScript::aiOutput`
- dialog output timing through `messageTime(output_name)`

Server module:

- `mmo_server_conversation_authority.h/.cpp`

This is safe scaffolding. The server stores the accepted line, timing and
elapsed/remaining state. It still does not choose dialog branches itself.

### Quest/dialog/script state commands

Server modules:

- `mmo_server_quest_authority.*`
- `mmo_server_dialog_authority.*`
- `mmo_server_script_authority.*`

These modules are mostly normalizers/validators for client-observed script
effects. They should be treated as event ingestion, not final server-side
Daedalus execution.

The quest status mapping follows the durable Gothic mapping already documented:

- `1=running`
- `2=success`
- `3=failed`
- `4=obsolete`

## Yellow: acceptable bridge, but not final Gothic AI

### NPC activity registry and priority

Server module:

- `mmo_server_npc_activity_authority.*`

Risk:

The registry's activity priority model is server-designed:

- routine/moving/talking/sleeping/using mob/alert/combat/down/dead priorities
- preemption behavior
- same-sync-group continuation rule

This is useful MMO infrastructure, but it is not directly proven from Gothic AI.
It should remain a server scheduling policy, not be confused with original
client decision logic.

Required future parity work:

- inspect Gothic script states such as `ZS_Talk`, `ZS_Attack`, `ZS_MM_Attack`,
  mobsi/sleep states and perception handlers;
- confirm which states are interruptible;
- replace broad priority assumptions with rules derived from scripts/content.

### Combat outcome policy

Server module:

- `mmo_server_combat_outcome_authority.h`

Client source checked:

- `GameScript::isDead` uses `ZS_Dead`
- `GameScript::isUnconscious` uses `ZS_Unconscious`
- loot hook allows dead or unconscious NPCs
- damage hook reports `fatal=true` when hitpoints reach zero

Risk:

The server policy "fatal without kill permission becomes Down" is conservative,
but still not fully proven from Gothic scripts. Real Gothic behavior likely
depends on AI state, guild/species, scripted routines, crime/perception and
attacker/target relationship.

Required future parity work:

- inspect Gothic script symbols and states that decide murder vs knockout;
- inspect fight/perception/crime scripts;
- feed the server outcome policy with Gothic-derived fields instead of generic
  flags like `allow_kill` or `non_lethal`.

### Weapon-ready alert

Server path:

- `mmo_udp_server_direct_interactive_apply.inl`

Risk:

The server currently treats weapon ready as `Alert`, which can interrupt talking.
This is a reasonable MMO reaction scaffold, but not yet verified against Gothic's
exact NPC reaction scripts.

Required future parity work:

- inspect client/script perception handlers for weapon drawing near NPCs;
- distinguish "noticed weapon", "warned player", "call guard", "attack" and
  "ignore" outcomes.

## Red: must not be considered final until reworked

### Perception and attack range constants

Server module:

- `mmo_server_gameplay_authority.h`

Risk:

The server currently has generic constants:

- `DefaultNpcPerceptionRange = 2200`
- `DefaultNpcFocusCos = 0.30`
- `DefaultMeleeWeaponRange = 150`
- `DefaultUnarmedRange = 90`

Client source checked:

- `game/game/fightalgo.cpp`
- `game/game/damagecalculator.cpp`
- `game/game/gamescript.cpp`

The client derives combat distances from `GameScript::guildVal()`,
`fight_range_base`, weapon state, weapon length, monster/fist/bow/magic ranges
and script variable `NPC_ATTACK_FINISH_DISTANCE`. Focus angle uses a 30-degree
cosine in `FightAlgo::isInFocusAngle`.

Conclusion:

Server `canPerceive` and `isInAttackRange` are only generic validators. They are
not Gothic-equivalent and should not drive final combat AI.

Required future parity work:

- create a server-side fight range model fed by imported Gothic guild values,
  weapon state and weapon/item range;
- mirror `FightAlgo::prefferedAttackDistance`,
  `prefferedGDistance`, `attackFinishDistance`, focus angle and weapon range;
- mirror `DamageCalculator` before server-owned damage is authoritative.

### Damage calculation

Server module:

- `mmo_server_combat_authority.h`

Risk:

Current server damage validation checks ranges and payload shape. It does not
calculate Gothic melee/ranged/spell/fall damage.

Client source checked:

- `game/game/damagecalculator.cpp`

The client uses weapon damage type, strength/dexterity, protections, hit chance,
critical chance, monster special cases, bow/magic ranges and collision masks.

Conclusion:

Server damage is not yet authoritative Gothic damage. It is validated ingestion
of client-observed damage.

Required future parity work:

- port or share `DamageCalculator` semantics server-side;
- import all required NPC/item/protection/talent/guild values;
- let the server produce damage, and demote client damage to proposal/evidence.

### NPC routine/path AI

Server modules:

- `mmo_server_waypoint_authority.*`
- `mmo_server_waypoint_graph.*`

Risk:

These modules validate observed routine/path state and graph adjacency. They do
not yet reproduce Gothic routine selection, freepoint/mobsi choice or path queue
behavior.

Conclusion:

Good foundation, not final AI.

Required future parity work:

- inspect client routine/TA/freepoint/mobsi resolution;
- implement server routine engine from world time plus imported schedule data;
- use waypoint graph validation as a safety layer, not the whole AI.

## Practical next order

1. Build a `server_fight_model` that mirrors `FightAlgo` using imported Gothic
   guild values and weapon/item data.
2. Build a `server_damage_model` that mirrors `DamageCalculator`.
3. Build `npc_threat_intent` from inspected Gothic perception/crime scripts:
   weapon draw, theft, attack, friendly fire, guild attitude, party member.
4. Replace generic `Alert/Combat/Down/Dead` inputs with outputs from that
   inspected threat/combat model.
5. Only after that start full server NPC scheduler: chase, attack, warn,
   call guards, loot/rob, return to routine.

## Bottom line

The current server-side AI work is structurally useful and modular, but only a
small part is already Gothic-equivalent. The highest-risk areas are combat
distance, perception, damage and lethal/non-lethal outcome. Those must be ported
from client/script behavior before the server is allowed to make final gameplay
decisions in production.
