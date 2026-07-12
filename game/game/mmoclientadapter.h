#pragma once

#include <cstdint>
#include <string_view>

namespace Mmo {

enum class ClientMmoSubmitStatus : std::uint8_t {
  Disabled,
  Accepted,
  InvalidIntent,
  UnsupportedIntent,
  QueueFull,
  TransportError,
};

struct ClientMmoSubmitResult final {
  ClientMmoSubmitStatus status = ClientMmoSubmitStatus::Disabled;
  std::uint64_t droppedCount = 0;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == ClientMmoSubmitStatus::Accepted ||
           status == ClientMmoSubmitStatus::Disabled;
  }
};

enum class ClientMovementState : std::uint16_t {
  None = 0,
  InAir = 1U << 0U,
  Falling = 1U << 1U,
  FallingDeep = 1U << 2U,
  Sliding = 1U << 3U,
  Jumping = 1U << 4U,
  JumpingUp = 1U << 5U,
  Swimming = 1U << 6U,
  Diving = 1U << 7U,
  InWater = 1U << 8U,
};

[[nodiscard]] constexpr ClientMovementState operator|(
    const ClientMovementState lhs,
    const ClientMovementState rhs) noexcept {
  return static_cast<ClientMovementState>(
      static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs));
}

constexpr ClientMovementState& operator|=(ClientMovementState& lhs,
                                          const ClientMovementState rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] constexpr bool hasMovementState(
    const ClientMovementState value,
    const ClientMovementState flag) noexcept {
  return (static_cast<std::uint16_t>(value) & static_cast<std::uint16_t>(flag)) != 0U;
}

struct ClientMovementSample final {
  std::uint64_t tick = 0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double yaw = 0.0;
  ClientMovementState state = ClientMovementState::None;
};

struct ClientMovementCadence final {
  std::uint64_t intervalMs = 0;
  double minimumDistance = 0.0;
  double minimumYawDegrees = 0.0;
};

// Engine-facing movement proposal. This DTO deliberately contains no packet
// kind, route header, sequence number, codec field or transport handle. The
// adapter copies it into the current sandbox facade contract synchronously.
// All string views must remain valid only for the duration of the call.
struct ClientMovementIntent final {
  ClientMovementSample from;
  ClientMovementSample to;
  ClientMovementCadence cadence;
  std::string_view targetKey;
  std::string_view source;
  std::string_view actorKey;
  std::string_view characterKey;
  std::string_view world;
  std::string_view waypointKey;
  std::string_view reason;
};

[[nodiscard]] ClientMmoSubmitResult submitClientMovement(
    const ClientMovementIntent& intent) noexcept;

} // namespace Mmo
