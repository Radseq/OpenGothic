#include "mmo_server_fight_intent_authority.h"

#include "mmo_server_combat_spatial_authority.h"

namespace Mmo::Server::FightIntent {

namespace {

[[nodiscard]] CombatSpatial::Input spatialInput(const CombatTimeline::Snapshot& snapshot,
                                                double focusDot) noexcept {
  return {
    .attackerCenter = snapshot.attackerCenter,
    .targetCenter = snapshot.opponentCenter,
    .attackerYawRad = snapshot.attackerYawRad,
    .attackRange = snapshot.attackRange,
    .attackerBaseRange = snapshot.attackerBaseRange,
    .targetBaseRange = snapshot.opponentBaseRange,
    .weaponRange = snapshot.weaponRange,
    .minFocusDot = focusDot,
    .hasAttackerCenter = snapshot.hasAttackerCenter,
    .hasTargetCenter = snapshot.hasOpponentCenter,
    .hasYaw = snapshot.hasAttackerYaw,
    .hasAttackRange = snapshot.hasAttackRange,
    .hasAttackerBaseRange = snapshot.hasAttackerBaseRange,
    .hasTargetBaseRange = snapshot.hasOpponentBaseRange,
    .hasWeaponRange = snapshot.hasWeaponRange,
  };
}

[[nodiscard]] bool isParade(const CombatTimeline::Snapshot& snapshot) noexcept {
  return snapshot.bodyState == 27;
}

[[nodiscard]] bool isParadeBodyState(std::int64_t bodyState) noexcept {
  return (bodyState & 0x7fff) == 27;
}

} // namespace

IntentState intentStateFromName(std::string_view name) noexcept {
  if(name == "proposed")
    return IntentState::Proposed;
  if(name == "accepted")
    return IntentState::Accepted;
  if(name == "rejected")
    return IntentState::Rejected;
  return IntentState::Observed;
}

std::string_view intentStateName(IntentState state) noexcept {
  switch(state) {
    case IntentState::Observed: return "observed";
    case IntentState::Proposed: return "proposed";
    case IntentState::Accepted: return "accepted";
    case IntentState::Rejected: return "rejected";
  }
  return "observed";
}

FightMove::Action observedAction(std::string_view attackState,
                                 std::int64_t bodyState,
                                 bool attackAnim,
                                 bool prehit) noexcept {
  if(isParadeBodyState(bodyState))
    return FightMove::Action::Block;
  if(attackAnim || prehit || attackState == "attack" || attackState == "attack_anim")
    return FightMove::Action::Attack;
  return FightMove::Action::None;
}

CorrelationResult Registry::apply(const IntentEvent& event) {
  if(event.actorKey.empty())
    return {.accepted = true, .reason = "missing_actor"};

  const std::string actorKey(event.actorKey);
  if(event.state == IntentState::Proposed) {
    pending_[actorKey] = PendingIntent {
      .actorKey = actorKey,
      .targetKey = std::string(event.targetKey),
      .actionName = std::string(event.actionName),
      .action = event.action,
      .proposedTickMs = event.serverTickMs,
    };
    return {.accepted = true, .reason = "proposal_stored", .storedProposal = true};
  }

  if(event.state == IntentState::Accepted) {
    const auto it = pending_.find(actorKey);
    if(it == pending_.end())
      return {.accepted = false, .reason = "accepted_without_proposed"};

    const auto& pendingIntent = it->second;
    if(pendingIntent.action != event.action) {
      pending_.erase(it);
      return {.accepted = false, .reason = "accepted_action_mismatch"};
    }
    if(!event.targetKey.empty() && !pendingIntent.targetKey.empty() && pendingIntent.targetKey != event.targetKey) {
      pending_.erase(it);
      return {.accepted = false, .reason = "accepted_target_mismatch"};
    }
    pending_.erase(it);
    return {.accepted = true, .reason = "accepted_matched_proposal", .matchedProposal = true};
  }

  if(event.state == IntentState::Rejected) {
    pending_.erase(actorKey);
    return {.accepted = true, .reason = "proposal_rejected"};
  }

  return {};
}

std::optional<PendingIntent> Registry::pending(std::string_view actorKey) const {
  const auto it = pending_.find(std::string(actorKey));
  if(it == pending_.end())
    return std::nullopt;
  return it->second;
}

std::vector<PendingIntent> Registry::expire(std::uint64_t nowServerTickMs) {
  std::vector<PendingIntent> expired;
  for(auto it = pending_.begin(); it != pending_.end();) {
    if(nowServerTickMs > it->second.proposedTickMs + ProposedIntentTimeoutMs) {
      expired.push_back(it->second);
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
  return expired;
}

void Registry::clear() {
  pending_.clear();
}

Decision evaluate(const IntentInput& input, const CombatTimeline::Registry& timeline) noexcept {
  if(input.actorKey.empty())
    return {.hasTimeline = false, .accepted = true, .softOnly = true, .reason = "missing_actor"};

  const auto snapshot = timeline.snapshot(input.actorKey);
  if(!snapshot)
    return {.hasTimeline = false, .accepted = true, .softOnly = true, .reason = "no_timeline"};

  if(!input.targetKey.empty() && !snapshot->opponentKey.empty() && snapshot->opponentKey != input.targetKey)
    return {.hasTimeline = true, .accepted = false, .softOnly = true, .reason = "intent_target_mismatch"};

  const auto focus = CombatSpatial::evaluate(spatialInput(*snapshot, CombatSpatial::GothicFocusAngleCos30));
  const auto narrowFocus = CombatSpatial::evaluate(spatialInput(*snapshot, NarrowAttackFocusCos5));
  const auto execution = FightMove::validateExecution({
    .action = input.action,
    .weaponMode = FightMove::weaponModeFromName(snapshot->weaponState),
    .focus = focus.hasSpatial ? focus.inFocusAngle : true,
    .narrowFocus = narrowFocus.hasSpatial ? narrowFocus.inFocusAngle : true,
    .inWRange = focus.hasSpatial ? focus.inRange : true,
    .closeupJumpBack = false,
    .actorSwimming = false,
    .actorInParade = isParade(*snapshot),
    .targetDown = false,
  });
  return {
    .hasTimeline = true,
    .accepted = execution.legal,
    .softOnly = true,
    .reason = execution.reason,
  };
}

} // namespace Mmo::Server::FightIntent
