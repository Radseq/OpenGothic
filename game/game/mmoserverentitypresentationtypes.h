#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace Mmo::ClientPresentation {

struct ServerEntityHandle final {
  std::uint64_t id = 0;
  std::uint32_t generation = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return id != 0U && generation != 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ServerEntityHandle&,
      const ServerEntityHandle&) noexcept = default;
};

enum class ServerEntityKind : std::uint8_t {
  LocalPlayer,
  RemotePlayer,
  Npc,
};

[[nodiscard]] constexpr bool isPlayerEntity(
    const ServerEntityKind kind) noexcept {
  return kind == ServerEntityKind::LocalPlayer ||
         kind == ServerEntityKind::RemotePlayer;
}

struct ServerPresentationRouteView final {
  std::uint64_t worldGeneration = 0;
  std::string_view worldInstanceId;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return worldGeneration != 0U && !worldInstanceId.empty();
  }
};

struct ServerPresentationRoute final {
  std::uint64_t worldGeneration = 0;
  std::string worldInstanceId;

  [[nodiscard]] bool valid() const noexcept {
    return worldGeneration != 0U && !worldInstanceId.empty();
  }

  [[nodiscard]] ServerPresentationRouteView view() const noexcept {
    return {.worldGeneration = worldGeneration,
            .worldInstanceId = worldInstanceId};
  }
};

struct ServerEntityTransformObservation final {
  ServerPresentationRouteView route;
  ServerEntityHandle handle;
  ServerEntityKind kind = ServerEntityKind::Npc;
  std::uint64_t serverTick = 0;
  std::string_view stableEntityKey;
  double posX = 0.0;
  double posY = 0.0;
  double posZ = 0.0;
  double yaw = 0.0;
  bool active = false;

  [[nodiscard]] bool valid() const noexcept {
    return route.valid() && handle.valid() && !stableEntityKey.empty() &&
           std::isfinite(posX) && std::isfinite(posY) &&
           std::isfinite(posZ) && std::isfinite(yaw);
  }
};

} // namespace Mmo::ClientPresentation
