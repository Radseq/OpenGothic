# MMO server combat swing/timeline audit - step 152

## Purpose

This is the client-derived map for melee/ranged combat timing. It must be used
before implementing server-owned attack decisions, block decisions, hit timing,
combo validation or damage application.

Do not replace this with a generic MMO combat model.

## Client files inspected

- `game/game/fightalgo.cpp`
- `game/game/fightalgo.h`
- `game/game/damagecalculator.cpp`
- `game/game/gamescript.cpp`
- `game/game/definitions/fightaidefinitions.cpp`
- `game/world/objects/npc.cpp`
- `game/graphics/mesh/animation.cpp`
- `game/graphics/mesh/animationsolver.cpp`
- `game/graphics/mesh/pose.cpp`
- `game/graphics/mdlvisual.cpp`

## Fight AI decision source

`FightAlgo::fillQueue` is the main NPC fight move selector.

It reads:

- `npc.handle().fight_tactic`
- loaded `Fight.dat` tables through `Gothic::fai()`
- weapon state
- distance to target
- focus angle
- whether the enemy is in `prehit`
- whether the attacker was recently hit

The move is chosen from Daedalus `C_FIGHTAI.MOVE` arrays loaded by
`FightAi::loadAi`.

Important fight AI buckets:

- `enemy_prehit`
- `enemy_stormprehit`
- `my_w_combo`
- `my_w_runto`
- `my_w_strafe`
- `my_w_focus`
- `my_w_nofocus`
- `my_g_combo`
- `my_g_runto`
- `my_g_strafe`
- `my_g_focus`
- `my_fk_focus`
- `my_g_fk_nofocus`
- `my_fk_focus_far`
- `my_fk_nofocus_far`
- `my_fk_focus_mag`
- `my_fk_nofocus_mag`

The server must import or reproduce these tables before it can choose attacks
like Gothic does.

## Fight moves

`FightAlgo::nextFromQueue` maps `zenkit::FightAiMove` to internal actions:

- `TURN` -> `MV_TURN`
- `RUN` -> `MV_MOVE`
- `JUMP_BACK` -> `MV_JUMPBACK`
- `STRAFE` -> `MV_STRAFEL/MV_STRAFER` plus `MV_STRAFE_E`
- `ATTACK` -> `MV_ATTACK`
- `ATTACK_SIDE` -> `MV_ATTACKL`, `MV_ATTACKR`
- `ATTACK_FRONT` -> randomized side attack then front attack
- `ATTACK_TRIPLE` -> randomized triple sequence
- `ATTACK_WHIRL` -> alternating left/right attacks
- `ATTACK_MASTER` -> left/right then repeated front attacks
- `TURN_TO_HIT` -> `MV_TURN2HIT`
- `PARRY` -> `MV_BLOCK`
- `WAIT`/`WAIT_EXT` -> `MV_WAIT`
- `WAIT_LONGER` -> `MV_WAITLONG`

This means a "swing" is not one fixed action. It can be a queued sequence of
attack actions driven by Fight.dat and combo windows.

## Distance and focus

Do not use generic server constants for final combat.

Client melee range is based on:

- `GameScript::guildVal().fight_range_base[target.guild()]`
- `GameScript::guildVal().fight_range_base[npc.guild()]`
- `FightAlgo::weaponRange`
- weapon state
- weapon item sword length
- guild values such as `fight_range_1ha`, `fight_range_2ha`,
  `fight_range_fist`, `fight_range_g`

Focus uses:

- `FightAlgo::isInFocusAngle` with 30 degrees for normal checks
- a tighter 5 degree focus check before attack execution in `Npc::implAttack`

`NPC_ATTACK_FINISH_DISTANCE` is read from scripts if present, otherwise defaults
to 180.

## Attack execution

`Npc::implAttack` executes actions from `FightAlgo`.

Important behavior:

- NPC attack does not run for player NPCs or talking NPCs.
- If target is down, fight queue is cleared.
- AI queue actions have priority over fight movement.
- if unarmed and allowed, NPC can auto-draw melee weapon.
- block action calls `blockFist` or `blockSword`.
- attack action checks tight focus and range.
- melee attack raycasts between collision centers; wall hit cancels attack.
- ranged/magic attack checks obstacle/friendly-fire-like logic before shooting.
- melee attack calls `doAttack(Attack/AttackL/AttackR, BS_HIT)`.
- after a melee attack starts, FAI waits for `visual.pose().atkTotalTime()+1`.

## Animation mapping

`AnimationSolver::implSolveAnim` maps abstract attack actions to MDS sequence
names.

Examples:

- fist attack:
  - `S_FISTATTACK`
  - `T_FISTATTACKMOVE`
- fist block:
  - G2: `T_FISTPARADE_0`
  - G1: `T_FISTPARADE_O`
- one/two handed attack:
  - moving front attack: `T_%sATTACKMOVE`
  - left: `T_%sATTACKL`
  - right: `T_%sATTACKR`
  - normal: `S_%sATTACK`
- one/two handed block:
  - G2 randomized among `T_%sPARADE_0`, `T_%sPARADE_0_A2`,
    `T_%sPARADE_0_A3`
  - G1: `T_%sPARADE_O`
- finishing move:
  - `T_%sSFINISH`
- bow:
  - aim: `S_%sAIM`
  - shoot: `S_%sSHOOT`
  - reload: `T_%sRELOAD`

Server-owned attack replay must preserve the accepted animation/key, weapon
state and start tick.

## Hit timing

Melee damage is not applied when the decision to attack is made.

Flow:

1. `Npc::doAttack` starts/continues an attack animation.
2. Animation events are processed in `Npc::tickAnimationTags`.
3. `Animation::Sequence::processEvent` increments `ev.def_opt_frame` on
   `zenkit::MdsEventType::OPTIMAL_FRAME`.
4. `Npc::tickAnimationTags` calls `commitDamage()` when `ev.def_opt_frame > 0`.
5. `Npc::commitDamage` checks:
   - current target exists,
   - target is in attack range,
   - attacker is in focus angle.
6. Only then `target.takeDamage(attacker, nullptr)` is called.

So the server needs an attack timeline with at least:

- attack animation id/key,
- server start tick,
- optimal frame time,
- hit end time,
- combo windows,
- current combo length,
- target key.

## Prehit

`Animation::Sequence::isPrehit` returns true when an attack animation has a
future `OPTIMAL_FRAME` still ahead of the current frame.

`FightAlgo::fillQueue` uses target `isPrehit` plus distance/focus checks to pick
`enemy_prehit` or `enemy_stormprehit` response moves. This is where block/parry
reaction starts.

The server must not approximate this as "enemy is attacking". It specifically
means "enemy is in an attack animation before its optimal frame".

## Blocking and jump-back defense

`Npc::takeDamage` checks defense at the moment damage arrives.

Block succeeds when:

- target is not already down,
- attack is not bullet/spell projectile (`b == nullptr` for melee),
- defender is in focus angle toward attacker,
- defender pose is in defense window:
  - `pose.isDefence(now)` checks layers with `BS_PARADE` and `defWindow`.

Jump-back can also avoid melee damage:

- `pose.isJumpBack(now)` plus `FightAlgo::isInJumpBackAngle`.

If blocked and defender has an active weapon, block effect is emitted.

This means the server needs authoritative defensive animation windows, not just
a boolean "blocking".

## Combo windows and interruption

Animation data stores:

- `HIT_END` -> `defHitEnd`
- `PARRY_FRAME` -> `defParFrame`
- `COMBO_WINDOW` -> `defWindow`

`Pose::continueCombo` allows attack continuation only inside the current combo
window. If the combo window is missed, combo breaks.

`Animation::Sequence::canInterrupt` also depends on combo/def windows. Damage
can interrupt only certain body states:

- if target body state has `BS_FLAG_INTERRUPTABLE`,
- or is `BS_RUN`,
- or is `BS_NONE`,
- and `bodystate_interruptable_override` is not set.

On hit, `Npc::takeDamage` can call `visual.interrupt()` and play stumble
animation unless damage type includes `FLY`.

## Damage

Damage is handled by `DamageCalculator`.

Server combat damage is not authoritative until it mirrors:

- melee sword/fist damage,
- ranged damage,
- spell damage,
- fall/drown damage,
- protection filtering,
- hit chance,
- critical chance,
- Gothic 1 vs Gothic 2 differences,
- monster special cases,
- collision masks such as `COLL_DONTKILL`.

## Change made in this step

`RecordNpcFightState` payload now includes extra client-observed phase data:

- `body_state`
- `attack_anim`
- `prehit`
- `weapon_state_id`

Existing common NPC fields already include:

- `dead`
- `unconscious`
- `down`
- current HP

This is still observation data. It does not make the server authoritative yet,
but it gives the future server fight model enough evidence to compare against
client behavior.

## Required server model before authoritative attacking

Build a server combat timeline module with these responsibilities:

1. Import or expose Fight.dat `FightAi` move tables.
2. Import guild fight range values and script constants.
3. Resolve attack animation key from abstract action plus weapon state.
4. Store active attack timeline:
   - actor,
   - target,
   - animation key,
   - start tick,
   - optimal frame tick,
   - hit end tick,
   - combo window ticks,
   - body state,
   - combo index.
5. At optimal frame, validate target range/focus and apply damage.
6. At incoming hit, validate defender's defense/jump-back window.
7. Validate combo continuation only inside combo window.
8. Emit authoritative events for clients to replay.

Until this exists, client-observed attack/damage events are only a bridge.
