#include "mmo_server_combat_spatial_authority.h"

#include <algorithm>
#include <cmath>

namespace Mmo::Server::CombatSpatial {

bool validRange(double range) noexcept {
  return std::isfinite(range) && range >= MinAttackRange && range <= MaxAttackRange;
}

Result evaluate(const Input& input) noexcept {
  Result out;
  if(!input.hasAttackerCenter || !input.hasTargetCenter ||
     !Gameplay::finitePosition(input.attackerCenter) ||
     !Gameplay::finitePosition(input.targetCenter))
    return out;

  out.hasSpatial = true;
  out.distanceSq = Gameplay::distanceSq2d(input.attackerCenter, input.targetCenter);
  const auto modelRange = CombatRange::preferredAttackDistance({
    .attackerBaseRange = input.attackerBaseRange,
    .targetBaseRange = input.targetBaseRange,
    .weaponRange = input.weaponRange,
    .hasAttackerBaseRange = input.hasAttackerBaseRange,
    .hasTargetBaseRange = input.hasTargetBaseRange,
    .hasWeaponRange = input.hasWeaponRange,
  });
  out.range = modelRange && validRange(*modelRange) ? *modelRange :
              input.hasAttackRange && validRange(input.attackRange) ? input.attackRange :
              Gameplay::DefaultMeleeWeaponRange;
  out.range = std::clamp(out.range + AttackRangeGrace, MinAttackRange, MaxAttackRange);
  out.inRange = out.distanceSq <= out.range * out.range;

  if(!input.hasYaw || !std::isfinite(input.attackerYawRad)) {
    out.inFocusAngle = true;
    return out;
  }

  const double dx = input.targetCenter.x - input.attackerCenter.x;
  const double dz = input.targetCenter.z - input.attackerCenter.z;
  const double len = std::sqrt(Gameplay::lengthSq2d(dx, dz));
  if(len <= MinForwardLength) {
    out.inFocusAngle = true;
    out.focusDot = 1.0;
    return out;
  }

  const double targetAngle = std::atan2(dz, dx);
  out.focusDot = std::cos(input.attackerYawRad - targetAngle);
  out.inFocusAngle = out.focusDot >= input.minFocusDot;
  return out;
}

} // namespace Mmo::Server::CombatSpatial
