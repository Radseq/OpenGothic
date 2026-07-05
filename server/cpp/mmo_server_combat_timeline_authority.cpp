#include "mmo_server_combat_timeline_authority.h"

#include <algorithm>
#include <cctype>

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

bool Registry::validInput(const ObservedFightInput& input) noexcept {
  if(input.comboIndex < 0 || input.comboIndex > MaxObservedComboIndex)
    return false;
  if(input.bodyState < 0 || input.bodyState > MaxObservedBodyState)
    return false;
  if(input.weaponStateId < 0 || input.weaponStateId > MaxObservedWeaponState)
    return false;
  return true;
}

bool Registry::timedOut(const Snapshot& snapshot, std::uint64_t nowServerTickMs) noexcept {
  if(isTerminalPhase(snapshot.phase))
    return false;
  return nowServerTickMs > snapshot.updatedServerTickMs + MaxObservedCombatStateMs;
}

} // namespace Mmo::Server::CombatTimeline
