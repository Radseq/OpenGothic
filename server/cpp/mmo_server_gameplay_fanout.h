#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mmo::Server {

enum class GameplayFanoutMode : std::uint8_t {
  TargetSessionOnly = 0,
  AreaOfInterest,
};

[[nodiscard]] constexpr const char* gameplayFanoutModeName(GameplayFanoutMode mode) noexcept {
  switch(mode) {
    case GameplayFanoutMode::TargetSessionOnly: return "target_session";
    case GameplayFanoutMode::AreaOfInterest:    return "aoi";
  }
  return "unknown";
}

enum class GameplayFanoutRejectReason : std::uint8_t {
  EmptySessionUuid = 0,
  MissingEndpoint,
  NotTargetInTargetOnlyMode,
  MissingOrigin,
  MissingObserverPosition,
  WorldMismatch,
  OutsideRadius,
  RecipientLimitReached,
};

[[nodiscard]] constexpr const char* gameplayFanoutRejectReasonName(GameplayFanoutRejectReason reason) noexcept {
  switch(reason) {
    case GameplayFanoutRejectReason::EmptySessionUuid:          return "empty_session_uuid";
    case GameplayFanoutRejectReason::MissingEndpoint:           return "missing_endpoint";
    case GameplayFanoutRejectReason::NotTargetInTargetOnlyMode: return "not_target_in_target_only_mode";
    case GameplayFanoutRejectReason::MissingOrigin:             return "missing_origin";
    case GameplayFanoutRejectReason::MissingObserverPosition:   return "missing_observer_position";
    case GameplayFanoutRejectReason::WorldMismatch:             return "world_mismatch";
    case GameplayFanoutRejectReason::OutsideRadius:             return "outside_radius";
    case GameplayFanoutRejectReason::RecipientLimitReached:     return "recipient_limit_reached";
  }
  return "unknown";
}

struct GameplayFanoutVec3 final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct GameplayFanoutObserver final {
  std::string sessionUuid;
  std::string characterKey;
  std::string worldName;
  GameplayFanoutVec3 position;
  std::uint64_t lastSeenAtMs = 0;
  bool hasPosition = false;
  bool endpointAvailable = false;
};

struct GameplayFanoutRequest final {
  GameplayFanoutMode mode = GameplayFanoutMode::TargetSessionOnly;
  std::string targetSessionUuid;
  std::string targetCharacterKey;
  std::string worldName;
  GameplayFanoutVec3 origin;
  double radius = 1800.0;
  std::size_t maxRecipients = 8;
  bool hasOrigin = false;
  bool requireEndpoint = true;
  bool includeTargetWithoutPosition = true;
};

struct GameplayFanoutRecipient final {
  std::string sessionUuid;
  std::string characterKey;
  std::string worldName;
  double distanceSquared = 0.0;
  std::uint64_t lastSeenAtMs = 0;
  bool isTarget = false;
  bool hasPosition = false;
};

struct GameplayFanoutRejectedObserver final {
  std::string sessionUuid;
  std::string characterKey;
  GameplayFanoutRejectReason reason = GameplayFanoutRejectReason::EmptySessionUuid;
  double distanceSquared = 0.0;
};

struct GameplayFanoutSelection final {
  GameplayFanoutMode mode = GameplayFanoutMode::TargetSessionOnly;
  std::string status = "not_selected";
  std::vector<GameplayFanoutRecipient> recipients;
  std::vector<GameplayFanoutRejectedObserver> rejected;
  std::size_t candidateCount = 0;
  std::size_t endpointMissingCount = 0;
  std::size_t worldMismatchCount = 0;
  std::size_t outsideRadiusCount = 0;
  std::size_t recipientLimitSkippedCount = 0;
  bool usedTargetPositionAsOrigin = false;
  bool targetObserved = false;

  [[nodiscard]] bool accepted() const noexcept { return !recipients.empty(); }
};

[[nodiscard]] constexpr bool finiteGameplayFanoutCoord(double value) noexcept {
  return value >= -10000000.0 && value <= 10000000.0;
}

[[nodiscard]] inline bool validGameplayFanoutPosition(const GameplayFanoutVec3& value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
         finiteGameplayFanoutCoord(value.x) && finiteGameplayFanoutCoord(value.y) && finiteGameplayFanoutCoord(value.z);
}

[[nodiscard]] constexpr double gameplayFanoutDistanceSquared(const GameplayFanoutVec3& a,
                                                             const GameplayFanoutVec3& b) noexcept {
  const double dx = a.x - b.x;
  const double dy = a.y - b.y;
  const double dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] inline bool sameGameplayFanoutWorld(std::string_view expected, std::string_view actual) noexcept {
  return expected.empty() || (!actual.empty() && expected == actual);
}

[[nodiscard]] inline bool isGameplayFanoutTarget(const GameplayFanoutRequest& request,
                                                 const GameplayFanoutObserver& observer) noexcept {
  if(!request.targetSessionUuid.empty() && request.targetSessionUuid == observer.sessionUuid)
    return true;
  return !request.targetCharacterKey.empty() && request.targetCharacterKey == observer.characterKey;
}

inline void rejectGameplayFanoutObserver(GameplayFanoutSelection& out,
                                         const GameplayFanoutObserver& observer,
                                         GameplayFanoutRejectReason reason,
                                         double distanceSquared = 0.0) {
  switch(reason) {
    case GameplayFanoutRejectReason::MissingEndpoint:
      ++out.endpointMissingCount;
      break;
    case GameplayFanoutRejectReason::WorldMismatch:
      ++out.worldMismatchCount;
      break;
    case GameplayFanoutRejectReason::OutsideRadius:
      ++out.outsideRadiusCount;
      break;
    case GameplayFanoutRejectReason::RecipientLimitReached:
      ++out.recipientLimitSkippedCount;
      break;
    default:
      break;
  }
  out.rejected.push_back({observer.sessionUuid, observer.characterKey, reason, distanceSquared});
}

[[nodiscard]] inline GameplayFanoutSelection selectGameplayFanoutRecipients(
    const std::vector<GameplayFanoutObserver>& observers,
    GameplayFanoutRequest request) {
  GameplayFanoutSelection out;
  out.mode = request.mode;
  out.candidateCount = observers.size();

  if(request.radius < 0.0 || !std::isfinite(request.radius))
    request.radius = 0.0;

  for(const auto& observer : observers) {
    if(isGameplayFanoutTarget(request, observer) && observer.hasPosition && validGameplayFanoutPosition(observer.position) && !request.hasOrigin) {
      request.origin = observer.position;
      request.hasOrigin = true;
      out.usedTargetPositionAsOrigin = true;
      break;
    }
  }

  const double radiusSquared = request.radius * request.radius;
  for(const auto& observer : observers) {
    if(observer.sessionUuid.empty()) {
      rejectGameplayFanoutObserver(out, observer, GameplayFanoutRejectReason::EmptySessionUuid);
      continue;
    }

    const bool isTarget = isGameplayFanoutTarget(request, observer);
    out.targetObserved = out.targetObserved || isTarget;

    if(request.requireEndpoint && !observer.endpointAvailable) {
      rejectGameplayFanoutObserver(out, observer, GameplayFanoutRejectReason::MissingEndpoint);
      continue;
    }

    if(request.mode == GameplayFanoutMode::TargetSessionOnly && !isTarget) {
      rejectGameplayFanoutObserver(out, observer, GameplayFanoutRejectReason::NotTargetInTargetOnlyMode);
      continue;
    }

    if(!isTarget && !sameGameplayFanoutWorld(request.worldName, observer.worldName)) {
      rejectGameplayFanoutObserver(out, observer, GameplayFanoutRejectReason::WorldMismatch);
      continue;
    }

    double distanceSquared = 0.0;
    if(request.mode == GameplayFanoutMode::AreaOfInterest && !isTarget) {
      if(!request.hasOrigin || !validGameplayFanoutPosition(request.origin)) {
        rejectGameplayFanoutObserver(out, observer, GameplayFanoutRejectReason::MissingOrigin);
        continue;
      }
      if(!observer.hasPosition || !validGameplayFanoutPosition(observer.position)) {
        rejectGameplayFanoutObserver(out, observer, GameplayFanoutRejectReason::MissingObserverPosition);
        continue;
      }
      distanceSquared = gameplayFanoutDistanceSquared(request.origin, observer.position);
      if(distanceSquared > radiusSquared) {
        rejectGameplayFanoutObserver(out, observer, GameplayFanoutRejectReason::OutsideRadius, distanceSquared);
        continue;
      }
    } else if(observer.hasPosition && validGameplayFanoutPosition(observer.position) && request.hasOrigin && validGameplayFanoutPosition(request.origin)) {
      distanceSquared = gameplayFanoutDistanceSquared(request.origin, observer.position);
    } else if(!isTarget || !request.includeTargetWithoutPosition) {
      rejectGameplayFanoutObserver(out, observer, GameplayFanoutRejectReason::MissingObserverPosition);
      continue;
    }

    out.recipients.push_back({observer.sessionUuid,
                              observer.characterKey,
                              observer.worldName,
                              distanceSquared,
                              observer.lastSeenAtMs,
                              isTarget,
                              observer.hasPosition});
  }

  std::stable_sort(out.recipients.begin(), out.recipients.end(), [](const GameplayFanoutRecipient& lhs,
                                                                    const GameplayFanoutRecipient& rhs) noexcept {
    if(lhs.isTarget != rhs.isTarget)
      return lhs.isTarget;
    if(lhs.distanceSquared != rhs.distanceSquared)
      return lhs.distanceSquared < rhs.distanceSquared;
    if(lhs.lastSeenAtMs != rhs.lastSeenAtMs)
      return lhs.lastSeenAtMs > rhs.lastSeenAtMs;
    return lhs.sessionUuid < rhs.sessionUuid;
  });

  if(request.maxRecipients > 0 && out.recipients.size() > request.maxRecipients) {
    for(std::size_t i = request.maxRecipients; i < out.recipients.size(); ++i) {
      GameplayFanoutObserver observer;
      observer.sessionUuid = out.recipients[i].sessionUuid;
      observer.characterKey = out.recipients[i].characterKey;
      rejectGameplayFanoutObserver(out, observer, GameplayFanoutRejectReason::RecipientLimitReached,
                                   out.recipients[i].distanceSquared);
    }
    out.recipients.resize(request.maxRecipients);
  }

  if(out.recipients.empty()) {
    out.status = out.targetObserved ? "no_reachable_recipient" : "target_not_observed";
  } else if(request.mode == GameplayFanoutMode::AreaOfInterest) {
    out.status = out.usedTargetPositionAsOrigin ? "aoi_selected_target_position_origin" : "aoi_selected";
  } else {
    out.status = "target_session_selected";
  }
  return out;
}

} // namespace Mmo::Server
