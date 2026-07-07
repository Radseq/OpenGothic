#pragma once

#include <cstdint>
#include <optional>

namespace Mmo::Server::CombatRange {

inline constexpr double MinRangePart = 0.0;
inline constexpr double MaxRangePart = 10000.0;

enum class WeaponKind : std::uint8_t {
  Unknown,
  NoWeapon,
  Fist,
  OneHanded,
  TwoHanded,
  Bow,
  Crossbow,
  Mage,
};

struct AttackRangeInput final {
  double attackerBaseRange = 0.0;
  double targetBaseRange = 0.0;
  double weaponRange = 0.0;
  bool hasAttackerBaseRange = false;
  bool hasTargetBaseRange = false;
  bool hasWeaponRange = false;
};

[[nodiscard]] bool validRangePart(double value) noexcept;
[[nodiscard]] std::optional<double> preferredAttackDistance(const AttackRangeInput& input) noexcept;

} // namespace Mmo::Server::CombatRange
