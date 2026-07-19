#pragma once

#include "mmoserverpresentationevents.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Mmo::ClientPresentation {

struct ServerProjectilePresentationConfig final {
  std::uint64_t interpolationDelayMs = 50U;
  std::uint64_t maximumExtrapolationMs = 100U;
  std::size_t maximumProjectiles = 4096U;
};

struct ServerProjectilePresentationSample final {
  std::uint64_t projectileId = 0U;
  std::uint64_t projectileArchetypeId = 0U;
  ServerPresentationEntityHandle owner{};
  double positionX = 0.0;
  double positionY = 0.0;
  double positionZ = 0.0;
  double velocityX = 0.0;
  double velocityY = 0.0;
  double velocityZ = 0.0;
  std::uint32_t radiusMillimeters = 0U;
  std::uint64_t stateRevision = 0U;
  bool impacted = false;

  [[nodiscard]] bool valid() const noexcept {
    return projectileId != 0U && projectileArchetypeId != 0U && owner.valid() &&
           std::isfinite(positionX) && std::isfinite(positionY) &&
           std::isfinite(positionZ) && std::isfinite(velocityX) &&
           std::isfinite(velocityY) && std::isfinite(velocityZ) &&
           radiusMillimeters != 0U && stateRevision != 0U;
  }
};

enum class ServerProjectilePresentationStatus : std::uint8_t {
  Applied,
  Duplicate,
  Stale,
  Missing,
  IdentityMismatch,
  RouteMismatch,
  CapacityExceeded,
  Invalid,
};

class ServerProjectilePresentationRegistry final {
 public:
  explicit ServerProjectilePresentationRegistry(
      ServerProjectilePresentationConfig config = {})
      : config_(config) {
    config_.maximumProjectiles =
        std::max<std::size_t>(1U, config_.maximumProjectiles);
    entries_.reserve(std::min<std::size_t>(config_.maximumProjectiles, 4096U));
  }

  void resetRoute(const ServerWorldInstanceHandle world = {}) noexcept {
    world_ = world;
    entries_.clear();
  }

  [[nodiscard]] ServerProjectilePresentationStatus observe(
      const ServerPresentationProjectileSnapshot& value,
      const std::uint64_t receivedAtMs) {
    if(!value.valid() || receivedAtMs == 0U)
      return ServerProjectilePresentationStatus::Invalid;
    if(!world_.valid() || value.owner.world != world_)
      return ServerProjectilePresentationStatus::RouteMismatch;

    const auto found = entries_.find(value.projectileId);
    if(found == entries_.end()) {
      if(entries_.size() >= config_.maximumProjectiles)
        return ServerProjectilePresentationStatus::CapacityExceeded;
      Entry entry;
      entry.latest = makeSample(value, receivedAtMs);
      entries_.emplace(value.projectileId, std::move(entry));
      return ServerProjectilePresentationStatus::Applied;
    }

    auto& entry = found->second;
    if(!sameIdentity(entry.latest.state, value))
      return ServerProjectilePresentationStatus::IdentityMismatch;
    if(value.stateRevision <= entry.latest.state.stateRevision) {
      return value.stateRevision == entry.latest.state.stateRevision
                 ? ServerProjectilePresentationStatus::Duplicate
                 : ServerProjectilePresentationStatus::Stale;
    }
    entry.previous = entry.latest;
    entry.hasPrevious = true;
    entry.latest = makeSample(value, receivedAtMs);
    entry.impacted = false;
    return ServerProjectilePresentationStatus::Applied;
  }

  [[nodiscard]] ServerProjectilePresentationStatus impact(
      const ServerPresentationProjectileImpact& value,
      const std::uint64_t receivedAtMs) noexcept {
    if(!value.valid() || receivedAtMs == 0U)
      return ServerProjectilePresentationStatus::Invalid;
    const auto found = entries_.find(value.projectileId);
    if(found == entries_.end())
      return ServerProjectilePresentationStatus::Missing;
    auto& entry = found->second;
    if(value.actionId != entry.latest.state.actionId)
      return ServerProjectilePresentationStatus::IdentityMismatch;
    if(value.stateRevision < entry.latest.state.stateRevision)
      return ServerProjectilePresentationStatus::Stale;
    entry.previous = entry.latest;
    entry.hasPrevious = true;
    entry.latest.state.positionXMicrometers = value.positionXMicrometers;
    entry.latest.state.positionYMicrometers = value.positionYMicrometers;
    entry.latest.state.positionZMicrometers = value.positionZMicrometers;
    entry.latest.state.velocityXMicrometersPerSecond = 0;
    entry.latest.state.velocityYMicrometersPerSecond = 0;
    entry.latest.state.velocityZMicrometersPerSecond = 0;
    entry.latest.state.stateRevision = value.stateRevision;
    entry.latest.receivedAtMs = receivedAtMs;
    entry.impacted = true;
    return ServerProjectilePresentationStatus::Applied;
  }

  [[nodiscard]] ServerProjectilePresentationStatus despawn(
      const ServerProjectileDespawnEvent& value) noexcept {
    if(value.projectileId == 0U || value.stateRevision == 0U)
      return ServerProjectilePresentationStatus::Invalid;
    const auto found = entries_.find(value.projectileId);
    if(found == entries_.end())
      return ServerProjectilePresentationStatus::Missing;
    if(value.stateRevision < found->second.latest.state.stateRevision)
      return ServerProjectilePresentationStatus::Stale;
    entries_.erase(found);
    return ServerProjectilePresentationStatus::Applied;
  }

  void sample(const std::uint64_t nowMs,
              std::vector<ServerProjectilePresentationSample>& out) const {
    out.clear();
    out.reserve(entries_.size());
    for(const auto& [id, entry] : entries_) {
      auto value = sampleOne(id, entry, nowMs);
      if(value.valid())
        out.push_back(value);
    }
    std::sort(out.begin(), out.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.projectileId < rhs.projectileId;
    });
  }

  [[nodiscard]] const ServerPresentationProjectileSnapshot* find(
      const std::uint64_t projectileId) const noexcept {
    const auto found = entries_.find(projectileId);
    return found != entries_.end() ? &found->second.latest.state : nullptr;
  }

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

 private:
  static constexpr double MicrometersPerWorldUnit = 10'000.0;

  struct TimedState final {
    ServerPresentationProjectileSnapshot state{};
    std::uint64_t receivedAtMs = 0U;
  };

  struct Entry final {
    TimedState previous{};
    TimedState latest{};
    bool hasPrevious = false;
    bool impacted = false;
  };

  [[nodiscard]] static TimedState makeSample(
      const ServerPresentationProjectileSnapshot& value,
      const std::uint64_t receivedAtMs) noexcept {
    return {.state = value, .receivedAtMs = receivedAtMs};
  }

  [[nodiscard]] static bool sameIdentity(
      const ServerPresentationProjectileSnapshot& lhs,
      const ServerPresentationProjectileSnapshot& rhs) noexcept {
    return lhs.owner == rhs.owner && lhs.target == rhs.target &&
           lhs.launcherArchetypeId == rhs.launcherArchetypeId &&
           lhs.projectileArchetypeId == rhs.projectileArchetypeId &&
           lhs.actionId == rhs.actionId &&
           lhs.actionSequence == rhs.actionSequence &&
           lhs.contentRevision == rhs.contentRevision &&
           lhs.rulesetId == rhs.rulesetId &&
           lhs.actionProfileId == rhs.actionProfileId &&
           lhs.spawnTick == rhs.spawnTick;
  }

  [[nodiscard]] static double worldPosition(const std::int64_t value) noexcept {
    return static_cast<double>(value) / MicrometersPerWorldUnit;
  }

  [[nodiscard]] static double worldVelocity(const std::int64_t value) noexcept {
    return static_cast<double>(value) / MicrometersPerWorldUnit;
  }

  [[nodiscard]] ServerProjectilePresentationSample sampleOne(
      const std::uint64_t id,
      const Entry& entry,
      const std::uint64_t nowMs) const noexcept {
    const auto& latest = entry.latest;
    double x = worldPosition(latest.state.positionXMicrometers);
    double y = worldPosition(latest.state.positionYMicrometers);
    double z = worldPosition(latest.state.positionZMicrometers);
    const double velocityX = worldVelocity(
        latest.state.velocityXMicrometersPerSecond);
    const double velocityY = worldVelocity(
        latest.state.velocityYMicrometersPerSecond);
    const double velocityZ = worldVelocity(
        latest.state.velocityZMicrometersPerSecond);

    const auto renderTime = nowMs > config_.interpolationDelayMs
                                ? nowMs - config_.interpolationDelayMs
                                : 0U;
    if(!entry.impacted && entry.hasPrevious &&
       entry.previous.receivedAtMs < latest.receivedAtMs &&
       renderTime <= latest.receivedAtMs) {
      const auto elapsed = renderTime > entry.previous.receivedAtMs
                               ? renderTime - entry.previous.receivedAtMs
                               : 0U;
      const auto duration = latest.receivedAtMs - entry.previous.receivedAtMs;
      const auto alpha = std::clamp(
          static_cast<double>(elapsed) / static_cast<double>(duration),
          0.0, 1.0);
      x = std::lerp(worldPosition(entry.previous.state.positionXMicrometers), x,
                    alpha);
      y = std::lerp(worldPosition(entry.previous.state.positionYMicrometers), y,
                    alpha);
      z = std::lerp(worldPosition(entry.previous.state.positionZMicrometers), z,
                    alpha);
    } else if(!entry.impacted && renderTime > latest.receivedAtMs) {
      const auto elapsedMs = std::min(
          renderTime - latest.receivedAtMs,
          config_.maximumExtrapolationMs);
      const auto elapsedSeconds = static_cast<double>(elapsedMs) / 1000.0;
      x += velocityX * elapsedSeconds;
      y += velocityY * elapsedSeconds;
      z += velocityZ * elapsedSeconds;
    }

    return {
        .projectileId = id,
        .projectileArchetypeId = latest.state.projectileArchetypeId,
        .owner = latest.state.owner,
        .positionX = x,
        .positionY = y,
        .positionZ = z,
        .velocityX = entry.impacted ? 0.0 : velocityX,
        .velocityY = entry.impacted ? 0.0 : velocityY,
        .velocityZ = entry.impacted ? 0.0 : velocityZ,
        .radiusMillimeters = latest.state.radiusMillimeters,
        .stateRevision = latest.state.stateRevision,
        .impacted = entry.impacted,
    };
  }

  ServerProjectilePresentationConfig config_{};
  ServerWorldInstanceHandle world_{};
  std::unordered_map<std::uint64_t, Entry> entries_;
};

} // namespace Mmo::ClientPresentation
