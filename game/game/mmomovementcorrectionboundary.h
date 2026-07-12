#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "mmoserverentitypresentationtypes.h"

namespace Mmo::ClientPresentation {

struct ServerMovementCorrection final {
  ServerPresentationRoute route;
  ServerEntityHandle handle;
  std::uint64_t serverTick = 0;
  double posX = 0.0;
  double posY = 0.0;
  double posZ = 0.0;
  double yaw = 0.0;
  bool hardSnap = false;

  [[nodiscard]] bool valid() const noexcept;
};

enum class ServerMovementCorrectionStatus : std::uint8_t {
  Accepted,
  Duplicate,
  Stale,
  RouteMismatch,
  IdentityMismatch,
  LocalPlayerNotBound,
  Invalid,
};

class ServerMovementCorrectionBoundary final {
 public:
  void resetRoute(std::uint64_t worldGeneration) noexcept;

  [[nodiscard]] bool bindLocalPlayer(
      ServerEntityHandle handle,
      ServerPresentationRouteView route);

  void unbindLocalPlayer() noexcept;

  [[nodiscard]] ServerMovementCorrectionStatus observe(
      const ServerMovementCorrection& correction);

  [[nodiscard]] std::optional<ServerMovementCorrection> takePending() noexcept;
  [[nodiscard]] bool hasPending() const noexcept;

 private:
  std::uint64_t worldGeneration_ = 0;
  std::string worldInstanceId_;
  ServerEntityHandle localPlayer_;
  std::uint64_t lastServerTick_ = 0;
  std::optional<ServerMovementCorrection> pending_;
};

} // namespace Mmo::ClientPresentation
