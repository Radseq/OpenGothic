#include "mmo_server_combat_range_model.h"

#include <cmath>

namespace Mmo::Server::CombatRange {

bool validRangePart(double value) noexcept {
  return std::isfinite(value) && value >= MinRangePart && value <= MaxRangePart;
}

std::optional<double> preferredAttackDistance(const AttackRangeInput& input) noexcept {
  if(!input.hasAttackerBaseRange || !input.hasTargetBaseRange || !input.hasWeaponRange)
    return std::nullopt;
  if(!validRangePart(input.attackerBaseRange) ||
     !validRangePart(input.targetBaseRange) ||
     !validRangePart(input.weaponRange))
    return std::nullopt;
  return input.attackerBaseRange + input.targetBaseRange + input.weaponRange;
}

} // namespace Mmo::Server::CombatRange
