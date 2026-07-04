#include "mmo_server_movement_authority.h"

#include <cmath>

namespace Mmo::Server {

namespace {

[[nodiscard]] constexpr bool deltaMsInRange(std::int64_t deltaMs, bool staleTinyDelta) noexcept {
  return deltaMs >= MovementMinDeltaMs && (deltaMs <= MovementMaxDeltaMs || staleTinyDelta);
}

[[nodiscard]] bool coordOk(double value) noexcept {
  return std::isfinite(value) && std::abs(value) <= MovementMaxCoordAbs;
}

} // namespace

MovementValidationResult validateMovementProposal(const MovementProposalInput& input) noexcept {
  MovementValidationResult out;

  const double dx = input.toX - input.fromX;
  const double dy = input.toY - input.fromY;
  const double dz = input.toZ - input.fromZ;
  out.horizontalDistance = std::sqrt(dx * dx + dz * dz);
  out.totalDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
  out.verticalDelta = dy;

  const double seconds = input.deltaMs > 0 ? static_cast<double>(input.deltaMs) / 1000.0 : 0.0;
  const double verticalAbs = std::abs(dy);
  out.horizontalSpeed = seconds > 0.0 ? out.horizontalDistance / seconds : MovementMaxHorizontalSpeed + 1.0;
  out.verticalSpeed = seconds > 0.0 ? verticalAbs / seconds : MovementMaxVerticalSpeed + 1.0;
  out.staleTinyDelta = input.deltaMs > MovementMaxDeltaMs &&
                       out.totalDistance <= MovementMaxStaleTinyDistance &&
                       verticalAbs <= MovementMaxStaleTinyVerticalDelta;

  const bool coordsOk = coordOk(input.fromX) && coordOk(input.fromY) && coordOk(input.fromZ) &&
                        coordOk(input.toX) && coordOk(input.toY) && coordOk(input.toZ);
  const bool speedOk = out.totalDistance <= MovementMaxStepDistance &&
                       out.horizontalSpeed <= MovementMaxHorizontalSpeed;
  const bool verticalOk = (verticalAbs <= MovementMaxVerticalDelta &&
                           out.verticalSpeed <= MovementMaxVerticalSpeed) ||
                          (dy < 0.0 &&
                           verticalAbs <= MovementMaxFallDelta &&
                           out.verticalSpeed <= MovementMaxFallSpeed);

  out.accepted = coordsOk && deltaMsInRange(input.deltaMs, out.staleTinyDelta) && speedOk && verticalOk;
  return out;
}

} // namespace Mmo::Server
