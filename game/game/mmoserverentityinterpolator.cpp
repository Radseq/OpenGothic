#include "mmoserverentityinterpolator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Mmo::ClientPresentation {
namespace {

[[nodiscard]] bool finitePacket(
    const Net::ServerEntityTransformDeltaPacket& packet) noexcept {
  return std::isfinite(packet.posX) && std::isfinite(packet.posY) &&
         std::isfinite(packet.posZ) && std::isfinite(packet.yaw);
}

[[nodiscard]] double squaredDistance(double ax, double ay, double az,
                                     double bx, double by, double bz) noexcept {
  const auto dx = bx - ax;
  const auto dy = by - ay;
  const auto dz = bz - az;
  return dx * dx + dy * dy + dz * dz;
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
}

ServerEntityInterpolationIngestStatus ServerEntityInterpolator::ingest(
    const Net::ServerEntityTransformDeltaPacket& packet,
    std::uint64_t receivedAtMs) {
  if(packet.entityId == 0 || packet.generation == 0 ||
     packet.stableEntityKey.empty() || !finitePacket(packet)) {
    return ServerEntityInterpolationIngestStatus::Invalid;
  }

  const bool active =
      (packet.flags & Net::ServerEntityTransformActive) != 0U;
  auto found = tracks_.find(packet.entityId);
  if(!active) {
    if(found == tracks_.end())
      return ServerEntityInterpolationIngestStatus::IgnoredInactive;
    if(packet.generation < found->second.generation ||
       (packet.generation == found->second.generation &&
        packet.serverTick < found->second.latest.serverTick)) {
      return ServerEntityInterpolationIngestStatus::Stale;
    }
    if(packet.generation != found->second.generation)
      return ServerEntityInterpolationIngestStatus::IgnoredInactive;
    if(packet.stableEntityKey != found->second.stableEntityKey)
      return ServerEntityInterpolationIngestStatus::IdentityMismatch;
    tracks_.erase(found);
    return ServerEntityInterpolationIngestStatus::Removed;
  }

  const Sample next{
      .serverTick = packet.serverTick,
      .receivedAtMs = receivedAtMs,
      .posX = packet.posX,
      .posY = packet.posY,
      .posZ = packet.posZ,
      .yaw = packet.yaw,
  };

  if(found == tracks_.end()) {
    if(tracks_.size() >= config_.maxEntities)
      return ServerEntityInterpolationIngestStatus::CapacityExceeded;
    Track track;
    track.generation = packet.generation;
    track.stableEntityKey = packet.stableEntityKey;
    track.latest = next;
    tracks_.emplace(packet.entityId, std::move(track));
    return ServerEntityInterpolationIngestStatus::Accepted;
  }

  auto& track = found->second;
  if(packet.generation < track.generation ||
     (packet.generation == track.generation &&
      packet.serverTick <= track.latest.serverTick)) {
    return ServerEntityInterpolationIngestStatus::Stale;
  }
  if(packet.generation == track.generation &&
     packet.stableEntityKey != track.stableEntityKey) {
    return ServerEntityInterpolationIngestStatus::IdentityMismatch;
  }
  if(packet.generation > track.generation) {
    Track replacement;
    replacement.generation = packet.generation;
    replacement.stableEntityKey = packet.stableEntityKey;
    replacement.latest = next;
    track = std::move(replacement);
    return ServerEntityInterpolationIngestStatus::ReplacedGeneration;
  }

  track.previous = track.latest;
  track.latest = next;
  track.hasPrevious = true;
  return ServerEntityInterpolationIngestStatus::Accepted;
}

std::vector<ServerEntityPresentationTransform> ServerEntityInterpolator::sample(
    std::uint64_t nowMs) const {
  std::vector<ServerEntityPresentationTransform> out;
  out.reserve(tracks_.size());

  const auto renderTime = nowMs > config_.interpolationDelayMs
                              ? nowMs - config_.interpolationDelayMs
                              : 0U;
  const auto snapDistanceSquared = config_.snapDistance * config_.snapDistance;

  for(const auto& [entityId, track] : tracks_) {
    ServerEntityPresentationTransform value;
    value.entityId = entityId;
    value.generation = track.generation;
    value.serverTick = track.latest.serverTick;
    value.stableEntityKey = track.stableEntityKey;

    if(!track.hasPrevious) {
      value.posX = track.latest.posX;
      value.posY = track.latest.posY;
      value.posZ = track.latest.posZ;
      value.yaw = track.latest.yaw;
      value.snapped = true;
      out.push_back(std::move(value));
      continue;
    }

    const auto& a = track.previous;
    const auto& b = track.latest;
    const bool forceSnap = squaredDistance(a.posX, a.posY, a.posZ, b.posX, b.posY, b.posZ) > snapDistanceSquared ||
                           b.receivedAtMs <= a.receivedAtMs;
    if(forceSnap) {
      value.posX = b.posX;
      value.posY = b.posY;
      value.posZ = b.posZ;
      value.yaw = b.yaw;
      value.snapped = true;
      out.push_back(std::move(value));
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
      out.push_back(std::move(value));
      continue;
    }

    const auto extrapolateMs = std::min(
        renderTime - b.receivedAtMs, config_.maxExtrapolationMs);
    const auto sourceDuration = b.receivedAtMs - a.receivedAtMs;
    const auto ratio = sourceDuration == 0U
                           ? 0.0
                           : static_cast<double>(extrapolateMs) /
                                 static_cast<double>(sourceDuration);
    value.posX = b.posX + (b.posX - a.posX) * ratio;
    value.posY = b.posY + (b.posY - a.posY) * ratio;
    value.posZ = b.posZ + (b.posZ - a.posZ) * ratio;
    value.yaw = interpolateYaw(b.yaw, b.yaw + normalizeRadians(b.yaw - a.yaw), ratio);
    out.push_back(std::move(value));
  }

  std::sort(out.begin(), out.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.entityId < rhs.entityId;
  });
  return out;
}

void ServerEntityInterpolator::erase(std::uint64_t entityId) noexcept {
  tracks_.erase(entityId);
}

void ServerEntityInterpolator::clear() noexcept {
  tracks_.clear();
}

std::size_t ServerEntityInterpolator::size() const noexcept {
  return tracks_.size();
}

} // namespace Mmo::ClientPresentation
