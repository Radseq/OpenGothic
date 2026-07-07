#pragma once

#include <cstdint>

#include "mmo_server_combat_range_model.h"
#include "mmo_server_gameplay_authority.h"

namespace Mmo::Server::CombatSpatial {

inline constexpr double GothicFocusAngleCos30 = 0.86602540378443864676;
inline constexpr double MinAttackRange = 1.0;
inline constexpr double MaxAttackRange = 10000.0;
inline constexpr double AttackRangeGrace = 25.0;
inline constexpr double MinForwardLength = 0.0001;

struct Input final {
  Gameplay::Vec3 attackerCenter;
  Gameplay::Vec3 targetCenter;
  double attackerYawRad = 0.0;
  double attackRange = 0.0;
  double attackerBaseRange = 0.0;
  double targetBaseRange = 0.0;
  double weaponRange = 0.0;
  double minFocusDot = GothicFocusAngleCos30;
  bool hasAttackerCenter = false;
  bool hasTargetCenter = false;
  bool hasYaw = false;
  bool hasAttackRange = false;
  bool hasAttackerBaseRange = false;
  bool hasTargetBaseRange = false;
  bool hasWeaponRange = false;
};

struct Result final {
  bool hasSpatial = false;
  bool inRange = false;
  bool inFocusAngle = false;
  double distanceSq = 0.0;
  double range = 0.0;
  double focusDot = 1.0;
};

[[nodiscard]] bool validRange(double range) noexcept;
[[nodiscard]] Result evaluate(const Input& input) noexcept;

} // namespace Mmo::Server::CombatSpatial
