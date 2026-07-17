#include "mmosemantichooks_internal.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "mmoclientadapter.h"
#include "mmoclientbridge.h"
#include "world/world.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/waypoint.h"
#include "commandline.h"
#include "utils/versioninfo.h"

namespace Mmo::Hooks::Detail {

static std::string_view goToHintName(Npc::GoToHint hint) noexcept {
  switch(hint) {
    case Npc::GT_No:     return "none";
    case Npc::GT_Way:    return "waypoint";
    case Npc::GT_NextFp: return "next_freepoint";
    case Npc::GT_Enemy:  return "enemy";
    case Npc::GT_Item:   return "item";
    case Npc::GT_Point:  return "point";
    case Npc::GT_EnemyG: return "enemy_deprecated";
    case Npc::GT_Flee:   return "flee";
  }
  return "unknown";
}

static bool hasActiveRoutine(Npc& npc) {
  const auto routines = npc.routineSnapshot();
  for(const auto& routine : routines) {
    if(routine.active)
      return true;
    }
  return false;
}

static std::string routineStateName(Npc& npc) {
  if(npc.isDead())
    return "dead";
  if(npc.isUnconscious())
    return "unconscious";
  if(npc.isDown())
    return "down";
  if(npc.remainingPathPointCount() != 0 || npc.moveTargetWayPoint() != nullptr)
    return "moving";
  if(hasActiveRoutine(npc))
    return "routine_active";
  return "idle";
}

static std::string aiStateName(Npc& npc) {
  if(!npc.currentAiStateName().empty())
    return std::string(npc.currentAiStateName());
  if(npc.currentAiStateFunction() != 0)
    return scriptFunctionKey(npc.currentAiStateFunction());
  if(npc.isDead())
    return "dead";
  return "idle";
}

static std::string aiIntentName(Npc& npc) {
  if(npc.isDead())
    return "dead";
  if(npc.isUnconscious() || npc.isDown())
    return "down";
  if(npc.isAttack() || npc.isAttackAnim() || npc.stateVictim() != nullptr)
    return "combat";
  if(npc.remainingPathPointCount() != 0 || npc.moveTargetWayPoint() != nullptr)
    return "pathing";
  if(hasActiveRoutine(npc))
    return "routine";
  return "idle";
}

static std::string pathStateName(Npc& npc) {
  if(npc.isDead())
    return "dead";
  if(npc.remainingPathPointCount() != 0)
    return "pathing";
  if(npc.moveTargetWayPoint() != nullptr)
    return "moving_to_waypoint";
  if(npc.currentWayPoint() != nullptr)
    return "at_waypoint";
  return "idle";
}

static std::string fightStateName(Npc& npc) {
  if(npc.isDead())
    return "dead";
  if(npc.isUnconscious() || npc.isDown())
    return "down";
  if(npc.isAttack() || npc.isAttackAnim())
    return "attacking";
  if(npc.stateVictim() != nullptr || npc.target() != nullptr)
    return "engaged";
  if(npc.weaponState() != WeaponState::NoWeapon)
    return "ready";
  return "idle";
}

static std::string attackStateName(Npc& npc) {
  if(npc.isAttackAnim())
    return "attack_anim";
  if(npc.isAttack())
    return "attack";
  if(npc.weaponState() != WeaponState::NoWeapon)
    return "ready";
  return "none";
}

static std::string npcActionKey(Npc& npc) {
  if(npc.isDead())
    return "dead";
  if(npc.isUnconscious() || npc.isDown())
    return "down";
  if(npc.isTalk())
    return "talking";

  std::string state(npc.currentAiStateName());
  for(char& ch : state)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if(state.find("sleep") != std::string::npos || state.find("bed") != std::string::npos)
    return "sleeping";
  if(state.find("mob") != std::string::npos)
    return "using_mob";
  if(npc.isAttack() || npc.isAttackAnim() || npc.stateVictim() != nullptr || npc.target() != nullptr)
    return "combat";
  if(npc.remainingPathPointCount() != 0 || npc.moveTargetWayPoint() != nullptr)
    return "moving";
  if(hasActiveRoutine(npc))
    return "routine";
  return "idle";
}

static void appendWaypointPayload(std::string& out,
                           const World& world,
                           const char* field,
                           const WayPoint* waypoint) {
  const auto key = waypointKey(world, waypoint);
  out.append(",\"");
  out.append(field);
  out.append("_key\":");
  appendEscaped(out, key);
  out.append(",\"");
  out.append(field);
  out.append("\":");
  appendEscaped(out, waypointName(waypoint));
}

bool shouldCaptureWorldAiAction(Npc& actor, Npc* other) noexcept {
  if(!isLiveWorldTick(actor.world()))
    return false;
  // Step45: weapon transitions and NPC-vs-NPC combat/death are sparse semantic
  // events. Capture them for live world actors too, because Gothic AI reacts to
  // readied weapons and non-player fights can change durable world state.
  (void)other;
  return true;
}

bool shouldCapturePlayerRelated(Npc& actor, Npc* other) noexcept {
  if(!isLiveWorldTick(actor.world()))
    return false;
  if(actor.isPlayer())
    return true;
  if(other != nullptr && other->isPlayer())
    return true;
  return shouldCaptureWorldAiAction(actor, other);
}

static void appendObservedNpcCommon(std::string& out, Npc& actor, std::string_view target) {
  appendNpcIdentity(out, "actor_npc", actor);
  out.append(",\"npc_entity_key\":"); appendEscaped(out, target);
  out.append(",\"target_key\":"); appendEscaped(out, target);
  out.append(",\"display_name\":"); appendEscaped(out, actor.displayName());
  out.append(",\"health_current\":"); appendInt(out, actor.attribute(ATR_HITPOINTS));
  out.append(",\"health_max\":"); appendInt(out, actor.attribute(ATR_HITPOINTSMAX));
  out.append(",\"dead\":"); appendBool(out, actor.isDead());
  out.append(",\"unconscious\":"); appendBool(out, actor.isUnconscious());
  out.append(",\"down\":"); appendBool(out, actor.isDown());
}

void onNpcLifecycleChanged(Npc& actor,
                           Npc* sourceActor,
                           bool dead,
                           bool unconscious,
                           const char* sourceLocation) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || actor.isPlayer() || (!dead && !unconscious) ||
     !shouldCapturePlayerRelated(actor, sourceActor))
    return;
  auto& world = actor.world();
  auto target = npcEntityKey(world.name(), actor.persistentId(), actor.instanceSymbol());

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  if(sourceActor != nullptr)
    appendNpcIdentity(payload, "source_actor", *sourceActor);
  appendNpcIdentity(payload, "target_npc", actor);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"dead\":"); payload.append(dead ? "true" : "false");
  payload.append(",\"unconscious\":"); payload.append(unconscious ? "true" : "false");
  payload.append(",\"reason\":");
  appendEscaped(payload, sourceActor != nullptr && sourceActor->isPlayer() ? "player_npc_no_health" : "world_ai_npc_no_health");
  appendWorld(payload, world);
  appendVec3(payload, "target_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::MarkNpcDead, std::move(target), std::move(payload), world.tickCount());
}

void onObservedNpcAuthorityState(Npc& actor,
                                 const char* sourceLocation,
                                 const char* reason) noexcept {
  if(!isServerBoundClientModeEnabled() || (!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) ||
     actor.isPlayer() || !isLiveWorldTick(actor.world()))
    return;

  auto& world = actor.world();
  auto target = npcEntityKey(world.name(), actor.persistentId(), actor.instanceSymbol());
  const auto aiState = aiStateName(actor);
  const auto routineState = routineStateName(actor);
  const auto intent = aiIntentName(actor);
  const auto pathState = pathStateName(actor);
  const auto fightState = fightStateName(actor);
  const auto actionKey = npcActionKey(actor);
  Npc* opponentNpc = actor.stateVictim() != nullptr ? actor.stateVictim() :
                     actor.target() != nullptr ? actor.target() : actor.stateOther();
  const auto targetEntity = npcTargetKey(opponentNpc);
  const auto currentWp = actor.currentWayPoint();
  const auto targetWp = actor.moveTargetWayPoint() != nullptr ? actor.moveTargetWayPoint() : actor.currentTaPoint();

  std::string routinePayload;
  routinePayload.reserve(768);
  routinePayload.append("{\"source\":"); appendEscaped(routinePayload, sourceLocation);
  routinePayload.append(",\"reason\":"); appendEscaped(routinePayload, reason);
  appendObservedNpcCommon(routinePayload, actor, target);
  routinePayload.append(",\"routine_state\":"); appendEscaped(routinePayload, routineState);
  routinePayload.append(",\"schedule_key\":"); appendEscaped(routinePayload, scriptFunctionKey(actor.currentAiStateFunction()));
  appendWaypointPayload(routinePayload, world, "current_waypoint", currentWp);
  appendWaypointPayload(routinePayload, world, "target_waypoint", targetWp);
  submitObservedNpcState(SemanticActionKind::RecordNpcRoutineState, actor, target, std::move(routinePayload));

  std::string aiPayload;
  aiPayload.reserve(832);
  aiPayload.append("{\"source\":"); appendEscaped(aiPayload, sourceLocation);
  aiPayload.append(",\"reason\":"); appendEscaped(aiPayload, reason);
  appendObservedNpcCommon(aiPayload, actor, target);
  aiPayload.append(",\"ai_state\":"); appendEscaped(aiPayload, aiState);
  aiPayload.append(",\"ai_state_function\":"); appendUInt(aiPayload, actor.currentAiStateFunction());
  aiPayload.append(",\"ai_intent\":"); appendEscaped(aiPayload, intent);
  aiPayload.append(",\"ai_target_key\":"); appendEscaped(aiPayload, targetEntity);
  aiPayload.append(",\"perception_state\":");
  {
    std::string perception = "body:";
    appendUInt(perception, static_cast<std::uint64_t>(actor.bodyStateMasked()));
    perception.append(";weapon:");
    perception.append(weaponStateName(actor.weaponState()));
    perception.append(";move:");
    perception.append(goToHintName(actor.moveHint()));
    appendEscaped(aiPayload, perception);
  }
  submitObservedNpcState(SemanticActionKind::RecordNpcAiState, actor, target, std::move(aiPayload));

  std::string actionPayload;
  actionPayload.reserve(640);
  actionPayload.append("{\"source\":"); appendEscaped(actionPayload, sourceLocation);
  actionPayload.append(",\"reason\":"); appendEscaped(actionPayload, reason);
  appendObservedNpcCommon(actionPayload, actor, target);
  actionPayload.append(",\"action_key\":"); appendEscaped(actionPayload, actionKey);
  actionPayload.append(",\"action_name\":"); appendEscaped(actionPayload, actionKey);
  actionPayload.append(",\"action_state\":\"active\"");
  actionPayload.append(",\"action_target_key\":"); appendEscaped(actionPayload, targetEntity);
  actionPayload.append(",\"sync_group\":"); appendEscaped(actionPayload, target + ":" + actionKey);
  actionPayload.append(",\"routine_state\":"); appendEscaped(actionPayload, routineState);
  actionPayload.append(",\"ai_state\":"); appendEscaped(actionPayload, aiState);
  actionPayload.append(",\"path_state\":"); appendEscaped(actionPayload, pathState);
  submitObservedNpcState(SemanticActionKind::RecordNpcActionState, actor, target, std::move(actionPayload));

  std::string pathPayload;
  pathPayload.reserve(896);
  pathPayload.append("{\"source\":"); appendEscaped(pathPayload, sourceLocation);
  pathPayload.append(",\"reason\":"); appendEscaped(pathPayload, reason);
  appendObservedNpcCommon(pathPayload, actor, target);
  pathPayload.append(",\"path_state\":"); appendEscaped(pathPayload, pathState);
  pathPayload.append(",\"route_key\":"); appendEscaped(pathPayload, scriptFunctionKey(actor.currentAiStateFunction()));
  appendWaypointPayload(pathPayload, world, "current_waypoint", currentWp);
  appendWaypointPayload(pathPayload, world, "next_waypoint", actor.nextPathWayPoint());
  appendWaypointPayload(pathPayload, world, "target_waypoint", targetWp);
  pathPayload.append(",\"remaining_path_points\":"); appendUInt(pathPayload, actor.remainingPathPointCount());
  pathPayload.append(",\"move_hint\":"); appendEscaped(pathPayload, goToHintName(actor.moveHint()));
  const auto pos = actor.position();
  pathPayload.append(",\"pos_x\":"); appendFloat(pathPayload, pos.x);
  pathPayload.append(",\"pos_y\":"); appendFloat(pathPayload, pos.y);
  pathPayload.append(",\"pos_z\":"); appendFloat(pathPayload, pos.z);
  submitObservedNpcState(SemanticActionKind::RecordNpcPathState, actor, target, std::move(pathPayload));

  std::string fightPayload;
  fightPayload.reserve(768);
  fightPayload.append("{\"source\":"); appendEscaped(fightPayload, sourceLocation);
  fightPayload.append(",\"reason\":"); appendEscaped(fightPayload, reason);
  appendObservedNpcCommon(fightPayload, actor, target);
  fightPayload.append(",\"opponent_key\":"); appendEscaped(fightPayload, targetEntity);
  fightPayload.append(",\"fight_state\":"); appendEscaped(fightPayload, fightState);
  fightPayload.append(",\"attack_state\":"); appendEscaped(fightPayload, attackStateName(actor));
  fightPayload.append(",\"body_state\":"); appendUInt(fightPayload, static_cast<std::uint64_t>(actor.bodyStateMasked()));
  fightPayload.append(",\"attack_anim\":"); appendBool(fightPayload, actor.isAttackAnim());
  fightPayload.append(",\"prehit\":"); appendBool(fightPayload, actor.isPrehit());
  fightPayload.append(",\"weapon_state\":"); appendEscaped(fightPayload, weaponStateName(actor.weaponState()));
  fightPayload.append(",\"weapon_state_id\":"); appendUInt(fightPayload, static_cast<std::uint8_t>(actor.weaponState()));
  fightPayload.append(",\"attacker_yaw_rad\":"); appendFloat(fightPayload, actor.rotationRad());
  fightPayload.append(",\"weapon_range\":"); appendFloat(fightPayload, observedWeaponRange(actor));
  fightPayload.append(",\"attack_range\":"); appendFloat(fightPayload, observedAttackRange(actor, opponentNpc));
  fightPayload.append(",\"attacker_fight_range_base\":"); appendFloat(fightPayload, observedFightRangeBase(actor));
  appendVec3(fightPayload, "attacker_center", actor.collosionCenter());
  if(opponentNpc != nullptr) {
    fightPayload.append(",\"opponent_fight_range_base\":"); appendFloat(fightPayload, observedFightRangeBase(*opponentNpc));
    appendVec3(fightPayload, "opponent_center", opponentNpc->collosionCenter());
    appendVec3(fightPayload, "fight_distance", actor.fightDistanceTo(*opponentNpc));
    fightPayload.append(",\"opponent_yaw_rad\":"); appendFloat(fightPayload, opponentNpc->rotationRad());
    fightPayload.append(",\"actor_running\":"); appendBool(fightPayload, actor.bodyStateMasked() == BS_RUN);
    fightPayload.append(",\"opponent_running\":"); appendBool(fightPayload, opponentNpc->bodyStateMasked() == BS_RUN);
    fightPayload.append(",\"opponent_prehit\":"); appendBool(fightPayload, opponentNpc->isPrehit());
    fightPayload.append(",\"opponent_attack_range\":"); appendFloat(fightPayload, observedAttackRange(*opponentNpc, &actor));
  }
  submitObservedNpcState(SemanticActionKind::RecordNpcFightState, actor, target, std::move(fightPayload));
}

} // namespace Mmo::Hooks::Detail
