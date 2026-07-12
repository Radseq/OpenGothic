#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "mmoserverentitypresentationtypes.h"

namespace Mmo::ClientPresentation {

struct ServerEntityInterpolationConfig final {
  std::uint64_t interpolationDelayMs = 100;
  std::uint64_t maxExtrapolationMs = 120;
  double snapDistance = 600.0;
  std::size_t maxEntities = 4096;
};

struct ServerEntityPresentationTransform final {
  ServerEntityHandle handle;
  ServerEntityKind kind = ServerEntityKind::Npc;
  std::uint64_t worldGeneration = 0;
  std::uint64_t serverTick = 0;
  double posX = 0.0;
  double posY = 0.0;
  double posZ = 0.0;
  double yaw = 0.0;
  bool snapped = false;
};

enum class ServerEntityInterpolationIngestStatus : std::uint8_t {
  Accepted,
  Stale,
  IdentityMismatch,
  RouteMismatch,
  RebindRequired,
  UnsupportedEntityKind,
  Invalid,
  CapacityExceeded,
};

class ServerEntityInterpolator final {
 public:
  explicit ServerEntityInterpolator(ServerEntityInterpolationConfig config = {});

  void resetRoute(std::uint64_t worldGeneration) noexcept;

  [[nodiscard]] ServerEntityInterpolationIngestStatus ingest(
      const ServerEntityTransformObservation& observation,
      std::uint64_t receivedAtMs);

  void sample(std::uint64_t nowMs,
              std::vector<ServerEntityPresentationTransform>& out) const;

  [[nodiscard]] std::vector<ServerEntityPresentationTransform> sample(
      std::uint64_t nowMs) const;

  [[nodiscard]] bool erase(ServerEntityHandle handle,
                           std::uint64_t worldGeneration) noexcept;
  void clear() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

 private:
  struct Sample final {
    std::uint64_t serverTick = 0;
    std::uint64_t receivedAtMs = 0;
    double posX = 0.0;
    double posY = 0.0;
    double posZ = 0.0;
    double yaw = 0.0;
  };

  struct Track final {
    ServerEntityHandle handle;
    ServerEntityKind kind = ServerEntityKind::Npc;
    std::uint64_t worldGeneration = 0;
    std::string worldInstanceId;
    std::string stableEntityKey;
    Sample previous;
    Sample latest;
    bool hasPrevious = false;
  };

  [[nodiscard]] bool acceptRoute(
      const ServerPresentationRouteView& route);

  ServerEntityInterpolationConfig config_;
  std::uint64_t worldGeneration_ = 0;
  std::string worldInstanceId_;
  std::unordered_map<std::uint64_t, Track> tracks_;
};

} // namespace Mmo::ClientPresentation
