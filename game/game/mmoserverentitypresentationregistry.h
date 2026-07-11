#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../../../shared/net/mmo/mmonetprotocol.h"

namespace Mmo::ClientPresentation {

inline constexpr std::uint32_t InvalidLocalNpcId =
    std::numeric_limits<std::uint32_t>::max();

struct LocalNpcPresentationIdentity final {
  std::uint32_t localNpcId = InvalidLocalNpcId;
  std::uint32_t persistentId = 0;
  std::uint32_t instanceSymbol = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return localNpcId != InvalidLocalNpcId;
  }
};

struct ServerEntityPresentationBinding final {
  std::uint32_t generation = 0;
  std::uint64_t lastServerTick = 0;
  LocalNpcPresentationIdentity local;
  std::string stableEntityKey;
};

enum class ServerEntityObservationStatus : std::uint8_t {
  NeedsLocalBinding,
  ExistingBinding,
  Removed,
  IgnoredInactive,
  Stale,
  IdentityMismatch,
};

class ServerEntityPresentationRegistry final {
  public:
    [[nodiscard]] ServerEntityObservationStatus observe(
        const Net::ServerEntityTransformDeltaPacket& transform);

    [[nodiscard]] bool bind(
        const Net::ServerEntityTransformDeltaPacket& transform,
        LocalNpcPresentationIdentity local);

    [[nodiscard]] const ServerEntityPresentationBinding* find(
        std::uint64_t entityId) const noexcept;

    void touch(const Net::ServerEntityTransformDeltaPacket& transform) noexcept;
    void invalidate(std::uint64_t entityId) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::unordered_map<std::uint64_t, ServerEntityPresentationBinding> entries_;
};

} // namespace Mmo::ClientPresentation
