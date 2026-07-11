#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../../shared/net/mmo/mmonetprotocol.h"

namespace Mmo::ClientPresentation {

struct ServerEntityInterpolationConfig final {
  std::uint64_t interpolationDelayMs = 100;
  std::uint64_t maxExtrapolationMs = 120;
  double snapDistance = 600.0;
  std::size_t maxEntities = 4096;
};

struct ServerEntityPresentationTransform final {
  std::uint64_t entityId = 0;
  std::uint32_t generation = 0;
  std::uint64_t serverTick = 0;
  std::string stableEntityKey;
  double posX = 0.0;
  double posY = 0.0;
  double posZ = 0.0;
  double yaw = 0.0;
  bool snapped = false;
};

enum class ServerEntityInterpolationIngestStatus : std::uint8_t {
  Accepted,
  ReplacedGeneration,
  Removed,
  IgnoredInactive,
  Stale,
  IdentityMismatch,
  Invalid,
  CapacityExceeded,
};

class ServerEntityInterpolator final {
 public:
  explicit ServerEntityInterpolator(ServerEntityInterpolationConfig config = {});

  [[nodiscard]] ServerEntityInterpolationIngestStatus ingest(
      const Net::ServerEntityTransformDeltaPacket& packet,
      std::uint64_t receivedAtMs);

  [[nodiscard]] std::vector<ServerEntityPresentationTransform> sample(
      std::uint64_t nowMs) const;

  void erase(std::uint64_t entityId) noexcept;
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
    std::uint32_t generation = 0;
    std::string stableEntityKey;
    Sample previous;
    Sample latest;
    bool hasPrevious = false;
  };

  ServerEntityInterpolationConfig config_;
  std::unordered_map<std::uint64_t, Track> tracks_;
};

} // namespace Mmo::ClientPresentation
