#include "mmoserverentityinterpolator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Mmo::ClientPresentation {
namespace {

[[nodiscard]] double squaredDistance(double ax, double ay, double az,
                                     double bx, double by, double bz) noexcept {
  const auto dx = bx - ax;
  const auto dy = by - ay;
  const auto dz = bz - az;
  return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] double horizontalSquaredDistance(double ax, double az,
                                               double bx, double bz) noexcept {
  const auto dx = bx - ax;
  const auto dz = bz - az;
  return dx * dx + dz * dz;
}

[[nodiscard]] double normalizeRadians(double value) noexcept {
  constexpr double Pi = 3.14159265358979323846264338327950288;
  constexpr double TwoPi = Pi * 2.0;
  while(value > Pi)
    value -= TwoPi;
  while(value < -Pi)
    value += TwoPi;
  return value;
}

[[nodiscard]] double interpolateYaw(double from, double to, double t) noexcept {
  return normalizeRadians(from + normalizeRadians(to - from) * t);
}

[[nodiscard]] double lerp(double from, double to, double t) noexcept {
  return from + (to - from) * t;
}

} // namespace

ServerEntityInterpolator::ServerEntityInterpolator(
    ServerEntityInterpolationConfig config)
    : config_(config) {
  config_.maxEntities = std::max<std::size_t>(1, config_.maxEntities);
  config_.snapDistance = std::max(0.0, config_.snapDistance);
  config_.movementThreshold = std::max(0.0, config_.movementThreshold);
  if(!std::isfinite(config_.maximumHorizontalExtrapolationSpeed) ||
     config_.maximumHorizontalExtrapolationSpeed < 0.0) {
    config_.maximumHorizontalExtrapolationSpeed = 0.0;
  }
  if(!std::isfinite(config_.maximumVerticalExtrapolationSpeed) ||
     config_.maximumVerticalExtrapolationSpeed < 0.0) {
    config_.maximumVerticalExtrapolationSpeed = 0.0;
  }
  tracks_.reserve(config_.maxEntities);
}

void ServerEntityInterpolator::resetRoute(
    const std::uint64_t worldGeneration) noexcept {
  tracks_.clear();
  worldGeneration_ = worldGeneration;
  worldInstanceId_.clear();
}

bool ServerEntityInterpolator::acceptRoute(
    const ServerPresentationRouteView& route) {
  if(!route.valid() || route.worldGeneration != worldGeneration_)
    return false;
  if(worldInstanceId_.empty()) {
    worldInstanceId_.assign(route.worldInstanceId);
    return true;
  }
  return worldInstanceId_ == route.worldInstanceId;
}

ServerEntityInterpolationIngestStatus ServerEntityInterpolator::ingest(
    const ServerEntityTransformObservation& observation,
    const std::uint64_t receivedAtMs) {
  if(!observation.valid() || !observation.active)
    return ServerEntityInterpolationIngestStatus::Invalid;
  if(observation.kind == ServerEntityKind::LocalPlayer)
    return ServerEntityInterpolationIngestStatus::UnsupportedEntityKind;
  if(!acceptRoute(observation.route))
    return ServerEntityInterpolationIngestStatus::RouteMismatch;

  const Sample next{
      .serverTick = observation.serverTick,
      .receivedAtMs = receivedAtMs,
      .posX = observation.posX,
      .posY = observation.posY,
      .posZ = observation.posZ,
      .yaw = observation.yaw,
  };

  auto found = tracks_.find(observation.handle.id);
  if(found == tracks_.end()) {
    if(tracks_.size() >= config_.maxEntities)
      return ServerEntityInterpolationIngestStatus::CapacityExceeded;
    Track track;
    track.handle = observation.handle;
    track.kind = observation.kind;
    track.worldGeneration = observation.route.worldGeneration;
    track.worldInstanceId.assign(observation.route.worldInstanceId);
    track.stableEntityKey.assign(observation.stableEntityKey);
    track.latest = next;
    tracks_.emplace(observation.handle.id, std::move(track));
    return ServerEntityInterpolationIngestStatus::Accepted;
  }

  auto& track = found->second;
  if(observation.handle.generation < track.handle.generation ||
     (observation.handle == track.handle &&
      observation.serverTick <= track.latest.serverTick)) {
    return ServerEntityInterpolationIngestStatus::Stale;
  }
  if(observation.handle.generation > track.handle.generation)
    return ServerEntityInterpolationIngestStatus::RebindRequired;
  if(track.kind != observation.kind ||
     track.worldGeneration != observation.route.worldGeneration ||
     track.worldInstanceId != observation.route.worldInstanceId ||
     track.stableEntityKey != observation.stableEntityKey) {
    return ServerEntityInterpolationIngestStatus::IdentityMismatch;
  }

  track.previous = track.latest;
  track.latest = next;
  track.hasPrevious = true;
  return ServerEntityInterpolationIngestStatus::Accepted;
}

void ServerEntityInterpolator::sample(
    const std::uint64_t nowMs,
    std::vector<ServerEntityPresentationTransform>& out) const {
  out.clear();
  out.reserve(tracks_.size());

  const auto renderTime = nowMs > config_.interpolationDelayMs
                              ? nowMs - config_.interpolationDelayMs
                              : 0U;
  const auto snapDistanceSquared = config_.snapDistance * config_.snapDistance;
  const auto movementThresholdSquared =
      config_.movementThreshold * config_.movementThreshold;

  for(const auto& [entityId, track] : tracks_) {
    static_cast<void>(entityId);
    ServerEntityPresentationTransform value;
    value.handle = track.handle;
    value.kind = track.kind;
    value.worldGeneration = track.worldGeneration;
    value.serverTick = track.latest.serverTick;

    if(!track.hasPrevious) {
      value.posX = track.latest.posX;
      value.posY = track.latest.posY;
      value.posZ = track.latest.posZ;
      value.yaw = track.latest.yaw;
      value.snapped = true;
      out.push_back(value);
      continue;
    }

    const auto& a = track.previous;
    const auto& b = track.latest;
    const bool sourceMoving =
        horizontalSquaredDistance(a.posX, a.posZ, b.posX, b.posZ) >
        movementThresholdSquared;
    const bool forceSnap =
        squaredDistance(a.posX, a.posY, a.posZ,
                        b.posX, b.posY, b.posZ) > snapDistanceSquared ||
        b.receivedAtMs <= a.receivedAtMs;
    if(forceSnap) {
      value.posX = b.posX;
      value.posY = b.posY;
      value.posZ = b.posZ;
      value.yaw = b.yaw;
      value.snapped = true;
      out.push_back(value);
      continue;
    }

    if(renderTime <= b.receivedAtMs) {
      const auto elapsed = renderTime > a.receivedAtMs
                               ? renderTime - a.receivedAtMs
                               : 0U;
      const auto duration = b.receivedAtMs - a.receivedAtMs;
      const auto t = duration == 0U
                         ? 1.0
                         : std::clamp(static_cast<double>(elapsed) /
                                          static_cast<double>(duration),
                                      0.0, 1.0);
      value.posX = lerp(a.posX, b.posX, t);
      value.posY = lerp(a.posY, b.posY, t);
      value.posZ = lerp(a.posZ, b.posZ, t);
      value.yaw = interpolateYaw(a.yaw, b.yaw, t);
      value.moving = sourceMoving;
      out.push_back(value);
      continue;
    }

    const auto elapsedAfterLatest = renderTime - b.receivedAtMs;
    const auto extrapolateMs = std::min(
        elapsedAfterLatest, config_.maxExtrapolationMs);
    const auto sourceDuration = b.receivedAtMs - a.receivedAtMs;
    const auto sourceSeconds = static_cast<double>(sourceDuration) / 1000.0;
    auto velocityX = sourceSeconds == 0.0 ? 0.0 : (b.posX - a.posX) / sourceSeconds;
    auto velocityY = sourceSeconds == 0.0 ? 0.0 : (b.posY - a.posY) / sourceSeconds;
    auto velocityZ = sourceSeconds == 0.0 ? 0.0 : (b.posZ - a.posZ) / sourceSeconds;
    const auto horizontalSpeed = std::hypot(velocityX, velocityZ);
    if(horizontalSpeed > config_.maximumHorizontalExtrapolationSpeed &&
       horizontalSpeed > 0.0) {
      const auto scale = config_.maximumHorizontalExtrapolationSpeed /
                         horizontalSpeed;
      velocityX *= scale;
      velocityZ *= scale;
    }
    velocityY = std::clamp(
        velocityY,
        -config_.maximumVerticalExtrapolationSpeed,
        config_.maximumVerticalExtrapolationSpeed);
    const auto extrapolateSeconds = static_cast<double>(extrapolateMs) / 1000.0;
    value.posX = b.posX + velocityX * extrapolateSeconds;
    value.posY = b.posY + velocityY * extrapolateSeconds;
    value.posZ = b.posZ + velocityZ * extrapolateSeconds;
    value.yaw = interpolateYaw(
        b.yaw, b.yaw + normalizeRadians(b.yaw - a.yaw),
        sourceDuration == 0U
            ? 0.0
            : static_cast<double>(extrapolateMs) /
                  static_cast<double>(sourceDuration));
    value.moving = sourceMoving &&
                   elapsedAfterLatest <= config_.maxExtrapolationMs;
    out.push_back(value);
  }

  std::sort(out.begin(), out.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.handle.id < rhs.handle.id;
  });
}

std::vector<ServerEntityPresentationTransform> ServerEntityInterpolator::sample(
    const std::uint64_t nowMs) const {
  std::vector<ServerEntityPresentationTransform> out;
  sample(nowMs, out);
  return out;
}

bool ServerEntityInterpolator::erase(
    const ServerEntityHandle handle,
    const std::uint64_t worldGeneration) noexcept {
  auto found = tracks_.find(handle.id);
  if(found == tracks_.end() || found->second.handle != handle ||
     found->second.worldGeneration != worldGeneration) {
    return false;
  }
  tracks_.erase(found);
  return true;
}

void ServerEntityInterpolator::clear() noexcept {
  tracks_.clear();
  worldGeneration_ = 0;
  worldInstanceId_.clear();
}

std::size_t ServerEntityInterpolator::size() const noexcept {
  return tracks_.size();
}

} // namespace Mmo::ClientPresentation
