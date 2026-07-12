#include "mmomovementcorrectionboundary.h"

#include <cmath>
#include <utility>

namespace Mmo::ClientPresentation {

bool ServerMovementCorrection::valid() const noexcept {
  return route.valid() && handle.valid() &&
         std::isfinite(posX) && std::isfinite(posY) &&
         std::isfinite(posZ) && std::isfinite(yaw);
}

void ServerMovementCorrectionBoundary::resetRoute(
    const std::uint64_t worldGeneration) noexcept {
  worldGeneration_ = worldGeneration;
  worldInstanceId_.clear();
  localPlayer_ = {};
  lastServerTick_ = 0;
  pending_.reset();
}

bool ServerMovementCorrectionBoundary::bindLocalPlayer(
    const ServerEntityHandle handle,
    const ServerPresentationRouteView route) {
  if(!handle.valid() || !route.valid() ||
     route.worldGeneration != worldGeneration_) {
    return false;
  }
  if(!worldInstanceId_.empty() && worldInstanceId_ != route.worldInstanceId)
    return false;

  worldInstanceId_.assign(route.worldInstanceId);
  if(localPlayer_ != handle) {
    localPlayer_ = handle;
    lastServerTick_ = 0;
    pending_.reset();
  }
  return true;
}

void ServerMovementCorrectionBoundary::unbindLocalPlayer() noexcept {
  localPlayer_ = {};
  lastServerTick_ = 0;
  pending_.reset();
}

ServerMovementCorrectionStatus ServerMovementCorrectionBoundary::observe(
    const ServerMovementCorrection& correction) {
  if(!correction.valid())
    return ServerMovementCorrectionStatus::Invalid;
  if(correction.route.worldGeneration != worldGeneration_ ||
     worldInstanceId_.empty() ||
     worldInstanceId_ != correction.route.worldInstanceId) {
    return ServerMovementCorrectionStatus::RouteMismatch;
  }
  if(!localPlayer_.valid())
    return ServerMovementCorrectionStatus::LocalPlayerNotBound;
  if(correction.handle != localPlayer_)
    return ServerMovementCorrectionStatus::IdentityMismatch;
  if(correction.serverTick < lastServerTick_)
    return ServerMovementCorrectionStatus::Stale;
  if(correction.serverTick == lastServerTick_)
    return ServerMovementCorrectionStatus::Duplicate;

  lastServerTick_ = correction.serverTick;
  pending_ = correction;
  return ServerMovementCorrectionStatus::Accepted;
}

std::optional<ServerMovementCorrection>
ServerMovementCorrectionBoundary::takePending() noexcept {
  auto out = std::move(pending_);
  pending_.reset();
  return out;
}

bool ServerMovementCorrectionBoundary::hasPending() const noexcept {
  return pending_.has_value();
}

} // namespace Mmo::ClientPresentation
