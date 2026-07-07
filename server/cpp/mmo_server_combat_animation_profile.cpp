#include "mmo_server_combat_animation_profile.h"

#include <algorithm>

namespace Mmo::Server::CombatAnimation {

bool validWindow(std::uint64_t beginMs, std::uint64_t endMs) noexcept {
  return beginMs < endMs && endMs <= MaxAnimationWindowMs;
}

std::uint64_t normalizedHitEnd(const Timing& timing) noexcept {
  if(timing.attackHitEndMs > timing.attackOptimalMs && timing.attackHitEndMs <= MaxAnimationWindowMs)
    return timing.attackHitEndMs;
  if(timing.attackTotalMs > timing.attackOptimalMs && timing.attackTotalMs <= MaxAnimationWindowMs)
    return timing.attackTotalMs;
  return timing.attackOptimalMs;
}

Evaluation evaluateAt(const Timing& timing, std::uint64_t elapsedMs) noexcept {
  Evaluation out;

  if(timing.attackOptimalMs > 0 && timing.attackOptimalMs <= MaxAnimationWindowMs) {
    const auto hitEnd = normalizedHitEnd(timing);
    const auto hitBegin = timing.attackOptimalMs > HitWindowLeadGraceMs ? timing.attackOptimalMs - HitWindowLeadGraceMs : 0;
    const auto hitTail = std::min<std::uint64_t>(MaxAnimationWindowMs, hitEnd + HitWindowTailGraceMs);
    out.knownAttackWindow = true;
    out.beforeHit = elapsedMs < hitBegin;
    out.inHitWindow = hitBegin <= elapsedMs && elapsedMs <= hitTail;
    out.afterHit = elapsedMs > hitTail;
  }

  if(validWindow(timing.parryWindowStartMs, timing.parryWindowEndMs)) {
    const auto parryEnd = std::min<std::uint64_t>(MaxAnimationWindowMs, timing.parryWindowEndMs + DefenceWindowTailGraceMs);
    out.inParryWindow = timing.parryWindowStartMs <= elapsedMs && elapsedMs <= parryEnd;
  }

  if(validWindow(timing.comboWindowStartMs, timing.comboWindowEndMs))
    out.inComboWindow = timing.comboWindowStartMs < elapsedMs && elapsedMs <= timing.comboWindowEndMs;

  return out;
}

} // namespace Mmo::Server::CombatAnimation
