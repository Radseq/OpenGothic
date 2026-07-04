#pragma once

#include <cstdint>

namespace Mmo::Server {

inline constexpr double MovementMaxCoordAbs = 10000000.0;
inline constexpr std::int64_t MovementMinDeltaMs = 1;
inline constexpr std::int64_t MovementMaxDeltaMs = 60000;
inline constexpr double MovementMaxStepDistance = 2500.0;
inline constexpr double MovementMaxStaleTinyDistance = 300.0;
inline constexpr double MovementMaxStaleTinyVerticalDelta = 300.0;
inline constexpr double MovementMaxHorizontalSpeed = 2500.0;
inline constexpr double MovementMaxVerticalDelta = 1600.0;
inline constexpr double MovementMaxVerticalSpeed = 3500.0;
inline constexpr double MovementMaxFallDelta = 6000.0;
inline constexpr double MovementMaxFallSpeed = 12000.0;

struct MovementProposalInput final {
  double fromX = 0.0;
  double fromY = 0.0;
  double fromZ = 0.0;
  double toX = 0.0;
  double toY = 0.0;
  double toZ = 0.0;
  std::int64_t deltaMs = 0;
};

struct MovementValidationResult final {
  bool accepted = false;
  bool staleTinyDelta = false;
  double totalDistance = 0.0;
  double horizontalDistance = 0.0;
  double verticalDelta = 0.0;
  double horizontalSpeed = 0.0;
  double verticalSpeed = 0.0;
};

[[nodiscard]] MovementValidationResult validateMovementProposal(const MovementProposalInput& input) noexcept;

} // namespace Mmo::Server
