#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mmoserverentitypresentationtypes.h"

namespace Mmo::ClientPresentation {

inline constexpr std::uint32_t InvalidLocalNpcId =
    std::numeric_limits<std::uint32_t>::max();

struct LocalNpcPresentationIdentity final {
  std::uint32_t localNpcId = InvalidLocalNpcId;
  std::uint32_t persistentId = 0;
  std::uint32_t instanceSymbol = 0;
  std::uintptr_t localObjectToken = 0U;
  bool materializedByMmo = false;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return localNpcId != InvalidLocalNpcId;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const LocalNpcPresentationIdentity&,
      const LocalNpcPresentationIdentity&) noexcept = default;
};

struct ServerEntityPresentationBinding final {
  ServerEntityHandle handle;
  ServerEntityKind kind = ServerEntityKind::Npc;
  std::uint64_t worldGeneration = 0;
  std::uint64_t lastServerTick = 0;
  LocalNpcPresentationIdentity local;
  std::string worldInstanceId;
  std::string stableEntityKey;
};

enum class ServerEntityObservationStatus : std::uint8_t {
  NeedsLocalBinding,
  ExistingBinding,
  Removed,
  IgnoredInactive,
  Stale,
  IdentityMismatch,
  RouteMismatch,
  Invalid,
};

struct ServerEntityObservationResult final {
  ServerEntityObservationStatus status = ServerEntityObservationStatus::Invalid;
  std::optional<ServerEntityPresentationBinding> releasedBinding;

  ServerEntityObservationResult(
      const ServerEntityObservationStatus statusValue,
      std::optional<ServerEntityPresentationBinding> released = std::nullopt) noexcept
      : status(statusValue), releasedBinding(std::move(released)) {}

  [[nodiscard]] constexpr bool needsBinding() const noexcept {
    return status == ServerEntityObservationStatus::NeedsLocalBinding;
  }
};

class ServerEntityPresentationRegistry final {
  public:
    explicit ServerEntityPresentationRegistry(
        std::size_t maxBindings = 4096U);

    [[nodiscard]] std::vector<ServerEntityPresentationBinding> resetRoute(
        std::uint64_t worldGeneration);

    [[nodiscard]] ServerEntityObservationResult observe(
        const ServerEntityTransformObservation& transform);

    [[nodiscard]] bool bind(
        const ServerEntityTransformObservation& transform,
        LocalNpcPresentationIdentity local);

    [[nodiscard]] const ServerEntityPresentationBinding* find(
        ServerEntityHandle handle,
        std::uint64_t worldGeneration) const noexcept;
    [[nodiscard]] const ServerEntityPresentationBinding* findLocal(
        std::uintptr_t localObjectToken) const noexcept;

    void touch(const ServerEntityTransformObservation& transform) noexcept;

    [[nodiscard]] std::optional<ServerEntityPresentationBinding> invalidate(
        ServerEntityHandle handle,
        std::uint64_t worldGeneration) noexcept;

    [[nodiscard]] std::vector<ServerEntityPresentationBinding> releaseAll();
    void clear() noexcept;

    [[nodiscard]] std::uint64_t worldGeneration() const noexcept;
    [[nodiscard]] const std::string& worldInstanceId() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    [[nodiscard]] bool acceptRoute(
        const ServerPresentationRouteView& route);
    [[nodiscard]] bool routeMatches(
        const ServerPresentationRouteView& route) const noexcept;

    std::size_t maxBindings_ = 4096U;
    std::uint64_t worldGeneration_ = 0;
    std::string worldInstanceId_;
    std::unordered_map<std::uint64_t, ServerEntityPresentationBinding> entries_;
    std::unordered_map<std::uintptr_t, std::uint64_t> localToEntity_;
};

} // namespace Mmo::ClientPresentation
