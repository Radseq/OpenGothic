#pragma once

#include <cstdint>

namespace Mmo::Server::CombatOutcome {

enum class Result : std::uint8_t {
  None,
  Combat,
  Down,
  Dead,
};

struct Input final {
  bool fatalDamage = false;
  bool observedDead = false;
  bool observedUnconscious = false;
  bool targetIsPlayer = false;
  bool lethalRequested = false;
  bool nonLethalRequested = false;
  bool killAllowed = false;
};

[[nodiscard]] constexpr Result decide(const Input& input) noexcept {
  if(input.observedDead)
    return Result::Dead;
  if(input.observedUnconscious)
    return Result::Down;
  if(!input.fatalDamage)
    return Result::Combat;
  if(input.targetIsPlayer)
    return Result::Down;
  if(input.nonLethalRequested)
    return Result::Down;
  if(input.lethalRequested || input.killAllowed)
    return Result::Dead;
  return Result::Down;
}

[[nodiscard]] constexpr bool isTerminal(Result result) noexcept {
  return result == Result::Down || result == Result::Dead;
}

[[nodiscard]] constexpr const char* activityKey(Result result) noexcept {
  switch(result) {
    case Result::None:
      return "idle";
    case Result::Combat:
      return "combat";
    case Result::Down:
      return "down";
    case Result::Dead:
      return "dead";
  }
  return "idle";
}

[[nodiscard]] constexpr const char* reason(Result result) noexcept {
  switch(result) {
    case Result::None:
      return "none";
    case Result::Combat:
      return "combat";
    case Result::Down:
      return "non_lethal_down";
    case Result::Dead:
      return "lethal_death";
  }
  return "none";
}

} // namespace Mmo::Server::CombatOutcome
