#include "mmo_server_combat_timeline_authority.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Mmo::Server::CombatTimeline {

namespace {

inline constexpr std::int64_t BodyStateMask = 0x7fff;
inline constexpr std::int64_t BodyStateUnconscious = 22;
inline constexpr std::int64_t BodyStateDead = 23;
inline constexpr std::int64_t BodyStateHit = 26;
inline constexpr std::int64_t BodyStateParade = 27;

[[nodiscard]] std::string trimAscii(std::string_view text) {
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    text.remove_prefix(1);
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    text.remove_suffix(1);
  return std::string(text);
}

[[nodiscard]] std::string lowerAscii(std::string_view text) {
  std::string out = trimAscii(text);
  for(char& ch : out)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return out;
}

[[nodiscard]] std::int64_t maskedBodyState(std::int64_t bodyState) noexcept {
  return bodyState & BodyStateMask;
}

[[nodiscard]] bool isIdleText(std::string_view value) noexcept {
  return value.empty() || value == "idle" || value == "none" || value == "unknown";
}

} // namespace

std::string_view phaseName(Phase phase) noexcept {
  switch(phase) {
    case Phase::Idle:
      return "idle";
    case Phase::Ready:
      return "ready";
    case Phase::Windup:
      return "windup";
    case Phase::HitFrameCandidate:
      return "hit_frame_candidate";
    case Phase::Recovery:
      return "recovery";
    case Phase::Defence:
      return "defence";
    case Phase::Down:
      return "down";
    case Phase::Dead:
      return "dead";
  }
  return "idle";
}

bool isAttackPhase(Phase phase) noexcept {
  return phase == Phase::Windup || phase == Phase::HitFrameCandidate || phase == Phase::Recovery;
}

bool isTerminalPhase(Phase phase) noexcept {
  return phase == Phase::Down || phase == Phase::Dead;
}

ApplyResult Registry::applyObservedFight(const ObservedFightInput& input) {
  if(!validInput(input))
    return {false, "combat_timeline_observation_invalid"};

  const auto actorKey = trimAscii(input.actorKey);
  if(actorKey.empty())
    return {true, "combat_timeline_missing_actor", false, false};

  const Phase nextPhase = classify(input);
  auto& state = states_[actorKey];
  const bool phaseChanged = state.actorKey.empty() || state.phase != nextPhase;
  const bool hitFrameCandidate = state.phase == Phase::Windup && nextPhase == Phase::HitFrameCandidate;

  state.actorKey = actorKey;
  state.opponentKey = trimAscii(input.opponentKey);
  state.fightState = trimAscii(input.fightState);
  state.attackState = trimAscii(input.attackState);
  state.weaponState = trimAscii(input.weaponState);
  state.animationName = trimAscii(input.animationName);
  state.attackAnimationName = trimAscii(input.attackAnimationName);
  state.phase = nextPhase;
  if(phaseChanged || state.startedServerTickMs == 0)
    state.startedServerTickMs = input.serverTickMs;
  state.updatedServerTickMs = input.serverTickMs;
  if(hitFrameCandidate || nextPhase == Phase::HitFrameCandidate)
    state.lastHitCandidateTickMs = input.serverTickMs;
  if(nextPhase == Phase::Defence)
    state.lastDefenceTickMs = input.serverTickMs;
  state.animationElapsedMs = input.animationElapsedMs;
  state.attackAnimationElapsedMs = input.attackAnimationElapsedMs;
  state.animationTotalMs = input.animationTotalMs;
  state.attackTotalMs = input.attackTotalMs;
  state.attackOptimalMs = input.attackOptimalMs;
  state.attackHitEndMs = input.attackHitEndMs;
  state.parryWindowStartMs = input.parryWindowStartMs;
  state.parryWindowEndMs = input.parryWindowEndMs;
  state.comboWindowStartMs = input.comboWindowStartMs;
  state.comboWindowEndMs = input.comboWindowEndMs;
  state.attackerCenter = input.attackerCenter;
  state.opponentCenter = input.opponentCenter;
  state.attackerYawRad = input.attackerYawRad;
  state.opponentYawRad = input.opponentYawRad;
  state.attackRange = input.attackRange;
  state.opponentAttackRange = input.opponentAttackRange;
  state.attackerBaseRange = input.attackerBaseRange;
  state.opponentBaseRange = input.opponentBaseRange;
  state.weaponRange = input.weaponRange;
  state.fightTableChoice = std::string(FightMove::tableChoiceName(FightMove::selectTable(selectionFromInput(input))));
  state.hasAttackerCenter = input.hasAttackerCenter;
  state.hasOpponentCenter = input.hasOpponentCenter;
  state.hasAttackerYaw = input.hasAttackerYaw;
  state.hasOpponentYaw = input.hasOpponentYaw;
  state.hasAttackRange = input.hasAttackRange;
  state.hasOpponentAttackRange = input.hasOpponentAttackRange;
  state.hasAttackerBaseRange = input.hasAttackerBaseRange;
  state.hasOpponentBaseRange = input.hasOpponentBaseRange;
  state.hasWeaponRange = input.hasWeaponRange;
  state.actorRunning = input.actorRunning;
  state.opponentRunning = input.opponentRunning;
  state.opponentPrehit = input.opponentPrehit;
  state.opponentInWRange = input.opponentInWRange;
  state.opponentInFocus = input.opponentInFocus;
  state.comboIndex = input.comboIndex;
  state.bodyState = input.bodyState;
  state.weaponStateId = input.weaponStateId;
  state.attackAnim = input.attackAnim;
  state.prehit = input.prehit;
  state.down = input.down;
  state.dead = input.dead;
  state.unconscious = input.unconscious;

  return {
    .accepted = true,
    .reason = "ok",
    .phaseChanged = phaseChanged,
    .hitFrameCandidate = hitFrameCandidate,
  };
}

DamageEvidence Registry::evaluateDamageEvidence(const DamageEvidenceInput& input) const {
  if(input.projectileOrSpell)
    return {.hasAttackerTimeline = false, .plausibleMeleeHit = true, .reason = "projectile_or_spell_not_timeline_checked"};

  const auto attackerKey = trimAscii(input.attackerKey);
  if(attackerKey.empty())
    return {};

  const auto it = states_.find(attackerKey);
  if(it == states_.end())
    return {};

  const auto& state = it->second;
  if(!input.targetKey.empty() && !state.opponentKey.empty() && state.opponentKey != trimAscii(input.targetKey))
    return {.hasAttackerTimeline = true, .plausibleMeleeHit = false, .reason = "target_mismatch"};

  const auto spatial = CombatSpatial::evaluate(spatialFromSnapshot(state));
  if(spatial.hasSpatial) {
    if(!spatial.inRange)
      return {.hasAttackerTimeline = true, .plausibleMeleeHit = false, .reason = "outside_fight_range"};
    if(!spatial.inFocusAngle)
      return {.hasAttackerTimeline = true, .plausibleMeleeHit = false, .reason = "outside_focus_angle"};
  }

  const auto timing = timingFromSnapshot(state);
  const auto elapsedNow = state.attackAnimationElapsedMs + (input.serverTickMs > state.updatedServerTickMs ?
                          input.serverTickMs - state.updatedServerTickMs : 0);
  const auto animation = CombatAnimation::evaluateAt(timing, elapsedNow);
  if(animation.knownAttackWindow) {
    if(animation.inHitWindow)
      return {.hasAttackerTimeline = true, .plausibleMeleeHit = true, .reason = "animation_hit_window"};
    if(animation.beforeHit)
      return {.hasAttackerTimeline = true, .plausibleMeleeHit = false, .reason = "before_animation_hit_window"};
    return {.hasAttackerTimeline = true, .plausibleMeleeHit = false, .reason = "after_animation_hit_window"};
  }

  const bool recentHitCandidate = state.lastHitCandidateTickMs > 0 &&
                                  input.serverTickMs <= state.lastHitCandidateTickMs + ObservedAttackGraceMs;
  const bool activeAttack = isAttackPhase(state.phase) &&
                            input.serverTickMs <= state.updatedServerTickMs + ObservedAttackGraceMs;
  if(recentHitCandidate || activeAttack)
    return {.hasAttackerTimeline = true, .plausibleMeleeHit = true, .reason = "attack_timeline_plausible"};

  return {.hasAttackerTimeline = true, .plausibleMeleeHit = false, .reason = "outside_observed_attack_window"};
}

std::optional<Snapshot> Registry::snapshot(std::string_view actorKey) const {
  const auto it = states_.find(std::string(actorKey));
  if(it == states_.end())
    return std::nullopt;
  return it->second;
}

std::vector<Snapshot> Registry::snapshots() const {
  std::vector<Snapshot> out;
  out.reserve(states_.size());
  for(const auto& [_, state] : states_)
    out.push_back(state);
  return out;
}

void Registry::expire(std::uint64_t nowServerTickMs) {
  for(auto it = states_.begin(); it != states_.end();) {
    if(timedOut(it->second, nowServerTickMs))
      it = states_.erase(it);
    else
      ++it;
  }
}

void Registry::clear() {
  states_.clear();
}

Phase Registry::classify(const ObservedFightInput& input) noexcept {
  const auto bodyState = maskedBodyState(input.bodyState);
  if(input.dead || bodyState == BodyStateDead)
    return Phase::Dead;
  if(input.down || input.unconscious || bodyState == BodyStateUnconscious)
    return Phase::Down;
  if(bodyState == BodyStateParade)
    return Phase::Defence;
  if(input.attackAnim) {
    const auto animation = CombatAnimation::evaluateAt(timingFromInput(input), input.attackAnimationElapsedMs);
    if(animation.knownAttackWindow) {
      if(animation.beforeHit)
        return Phase::Windup;
      if(animation.inHitWindow)
        return Phase::HitFrameCandidate;
      if(animation.afterHit)
        return Phase::Recovery;
    }
  }
  if(input.prehit)
    return Phase::Windup;
  if(input.attackAnim || bodyState == BodyStateHit)
    return Phase::HitFrameCandidate;

  const auto attackState = lowerAscii(input.attackState);
  if(attackState == "attack" || attackState == "attack_anim")
    return Phase::HitFrameCandidate;
  if(attackState == "ready")
    return Phase::Ready;

  const auto fightState = lowerAscii(input.fightState);
  if(fightState == "attacking")
    return Phase::HitFrameCandidate;
  if(fightState == "engaged" || fightState == "ready")
    return Phase::Ready;
  if(isIdleText(fightState))
    return Phase::Idle;
  return Phase::Ready;
}

CombatAnimation::Timing Registry::timingFromInput(const ObservedFightInput& input) noexcept {
  return {
    .attackOptimalMs = input.attackOptimalMs,
    .attackHitEndMs = input.attackHitEndMs,
    .attackTotalMs = input.attackTotalMs,
    .parryWindowStartMs = input.parryWindowStartMs,
    .parryWindowEndMs = input.parryWindowEndMs,
    .comboWindowStartMs = input.comboWindowStartMs,
    .comboWindowEndMs = input.comboWindowEndMs,
  };
}

CombatAnimation::Timing Registry::timingFromSnapshot(const Snapshot& snapshot) noexcept {
  return {
    .attackOptimalMs = snapshot.attackOptimalMs,
    .attackHitEndMs = snapshot.attackHitEndMs,
    .attackTotalMs = snapshot.attackTotalMs,
    .parryWindowStartMs = snapshot.parryWindowStartMs,
    .parryWindowEndMs = snapshot.parryWindowEndMs,
    .comboWindowStartMs = snapshot.comboWindowStartMs,
    .comboWindowEndMs = snapshot.comboWindowEndMs,
  };
}

CombatSpatial::Input Registry::spatialFromInput(const ObservedFightInput& input) noexcept {
  return {
    .attackerCenter = input.attackerCenter,
    .targetCenter = input.opponentCenter,
    .attackerYawRad = input.attackerYawRad,
    .attackRange = input.attackRange,
    .attackerBaseRange = input.attackerBaseRange,
    .targetBaseRange = input.opponentBaseRange,
    .weaponRange = input.weaponRange,
    .minFocusDot = CombatSpatial::GothicFocusAngleCos30,
    .hasAttackerCenter = input.hasAttackerCenter,
    .hasTargetCenter = input.hasOpponentCenter,
    .hasYaw = input.hasAttackerYaw,
    .hasAttackRange = input.hasAttackRange,
    .hasAttackerBaseRange = input.hasAttackerBaseRange,
    .hasTargetBaseRange = input.hasOpponentBaseRange,
    .hasWeaponRange = input.hasWeaponRange,
  };
}

CombatSpatial::Input Registry::spatialFromSnapshot(const Snapshot& snapshot) noexcept {
  return {
    .attackerCenter = snapshot.attackerCenter,
    .targetCenter = snapshot.opponentCenter,
    .attackerYawRad = snapshot.attackerYawRad,
    .attackRange = snapshot.attackRange,
    .attackerBaseRange = snapshot.attackerBaseRange,
    .targetBaseRange = snapshot.opponentBaseRange,
    .weaponRange = snapshot.weaponRange,
    .minFocusDot = CombatSpatial::GothicFocusAngleCos30,
    .hasAttackerCenter = snapshot.hasAttackerCenter,
    .hasTargetCenter = snapshot.hasOpponentCenter,
    .hasYaw = snapshot.hasAttackerYaw,
    .hasAttackRange = snapshot.hasAttackRange,
    .hasAttackerBaseRange = snapshot.hasAttackerBaseRange,
    .hasTargetBaseRange = snapshot.hasOpponentBaseRange,
    .hasWeaponRange = snapshot.hasWeaponRange,
  };
}

FightMove::SelectionInput Registry::selectionFromInput(const ObservedFightInput& input) noexcept {
  const auto actorSpatial = CombatSpatial::evaluate(spatialFromInput(input));
  const auto opponentSpatial = CombatSpatial::evaluate({
    .attackerCenter = input.opponentCenter,
    .targetCenter = input.attackerCenter,
    .attackerYawRad = input.opponentYawRad,
    .attackRange = input.opponentAttackRange,
    .minFocusDot = CombatSpatial::GothicFocusAngleCos30,
    .hasAttackerCenter = input.hasOpponentCenter,
    .hasTargetCenter = input.hasAttackerCenter,
    .hasYaw = input.hasOpponentYaw,
    .hasAttackRange = input.hasOpponentAttackRange,
  });

  return {
    .weaponMode = FightMove::weaponModeFromName(input.weaponState),
    .hitFlag = false,
    .focus = actorSpatial.hasSpatial ? actorSpatial.inFocusAngle : false,
    .inWRange = actorSpatial.hasSpatial ? actorSpatial.inRange : false,
    .inGRange = actorSpatial.hasSpatial ? actorSpatial.inRange : false,
    .actorRunning = input.actorRunning,
    .targetPrehit = input.opponentPrehit,
    .targetInWRange = opponentSpatial.hasSpatial ? opponentSpatial.inRange : input.opponentInWRange,
    .targetFocus = opponentSpatial.hasSpatial ? opponentSpatial.inFocusAngle : input.opponentInFocus,
    .targetRunning = input.opponentRunning,
  };
}

bool Registry::validInput(const ObservedFightInput& input) noexcept {
  if(input.comboIndex < 0 || input.comboIndex > MaxObservedComboIndex)
    return false;
  if(input.bodyState < 0 || input.bodyState > MaxObservedBodyState)
    return false;
  if(input.weaponStateId < 0 || input.weaponStateId > MaxObservedWeaponState)
    return false;
  if(input.animationElapsedMs > CombatAnimation::MaxAnimationWindowMs ||
     input.attackAnimationElapsedMs > CombatAnimation::MaxAnimationWindowMs ||
     input.animationTotalMs > CombatAnimation::MaxAnimationWindowMs ||
     input.attackTotalMs > CombatAnimation::MaxAnimationWindowMs ||
     input.attackOptimalMs > CombatAnimation::MaxAnimationWindowMs ||
     input.attackHitEndMs > CombatAnimation::MaxAnimationWindowMs ||
     input.parryWindowStartMs > CombatAnimation::MaxAnimationWindowMs ||
     input.parryWindowEndMs > CombatAnimation::MaxAnimationWindowMs ||
     input.comboWindowStartMs > CombatAnimation::MaxAnimationWindowMs ||
     input.comboWindowEndMs > CombatAnimation::MaxAnimationWindowMs)
    return false;
  if(input.hasAttackerCenter && !Gameplay::finitePosition(input.attackerCenter))
    return false;
  if(input.hasOpponentCenter && !Gameplay::finitePosition(input.opponentCenter))
    return false;
  if(input.hasAttackerYaw && !std::isfinite(input.attackerYawRad))
    return false;
  if(input.hasOpponentYaw && !std::isfinite(input.opponentYawRad))
    return false;
  if(input.hasAttackRange && !CombatSpatial::validRange(input.attackRange))
    return false;
  if(input.hasOpponentAttackRange && !CombatSpatial::validRange(input.opponentAttackRange))
    return false;
  if(input.hasAttackerBaseRange && !CombatRange::validRangePart(input.attackerBaseRange))
    return false;
  if(input.hasOpponentBaseRange && !CombatRange::validRangePart(input.opponentBaseRange))
    return false;
  if(input.hasWeaponRange && !CombatRange::validRangePart(input.weaponRange))
    return false;
  return true;
}

bool Registry::timedOut(const Snapshot& snapshot, std::uint64_t nowServerTickMs) noexcept {
  if(isTerminalPhase(snapshot.phase))
    return false;
  return nowServerTickMs > snapshot.updatedServerTickMs + MaxObservedCombatStateMs;
}

} // namespace Mmo::Server::CombatTimeline
