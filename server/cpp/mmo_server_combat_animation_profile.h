#pragma once

#include <cstdint>

namespace Mmo::Server::CombatAnimation {

inline constexpr std::uint64_t HitWindowLeadGraceMs = 60;
inline constexpr std::uint64_t HitWindowTailGraceMs = 120;
inline constexpr std::uint64_t DefenceWindowTailGraceMs = 80;
inline constexpr std::uint64_t MaxAnimationWindowMs = 30000;

struct Timing final {
  std::uint64_t attackOptimalMs = 0;
  std::uint64_t attackHitEndMs = 0;
  std::uint64_t attackTotalMs = 0;
  std::uint64_t parryWindowStartMs = 0;
  std::uint64_t parryWindowEndMs = 0;
  std::uint64_t comboWindowStartMs = 0;
  std::uint64_t comboWindowEndMs = 0;
};

struct Evaluation final {
  bool knownAttackWindow = false;
  bool beforeHit = false;
  bool inHitWindow = false;
  bool afterHit = false;
  bool inParryWindow = false;
  bool inComboWindow = false;
};

[[nodiscard]] bool validWindow(std::uint64_t beginMs, std::uint64_t endMs) noexcept;
[[nodiscard]] std::uint64_t normalizedHitEnd(const Timing& timing) noexcept;
[[nodiscard]] Evaluation evaluateAt(const Timing& timing, std::uint64_t elapsedMs) noexcept;

} // namespace Mmo::Server::CombatAnimation
