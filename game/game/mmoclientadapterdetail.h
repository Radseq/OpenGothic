#pragma once

#include "mmoclientadapter.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "../../../shared/game/mmo/mmosemanticevents.h"
#include "../../../shared/net/mmo/mmonetprotocol.h"

namespace Mmo::ClientAdapterDetail {

inline constexpr std::size_t MaximumTextSize = 4096U;
inline constexpr std::uint16_t KnownMovementStateMask =
    static_cast<std::uint16_t>(ClientMovementState::InAir) |
    static_cast<std::uint16_t>(ClientMovementState::Falling) |
    static_cast<std::uint16_t>(ClientMovementState::FallingDeep) |
    static_cast<std::uint16_t>(ClientMovementState::Sliding) |
    static_cast<std::uint16_t>(ClientMovementState::Jumping) |
    static_cast<std::uint16_t>(ClientMovementState::JumpingUp) |
    static_cast<std::uint16_t>(ClientMovementState::Swimming) |
    static_cast<std::uint16_t>(ClientMovementState::Diving) |
    static_cast<std::uint16_t>(ClientMovementState::InWater);

[[nodiscard]] inline bool validRequiredText(const std::string_view value) noexcept {
  return !value.empty() && value.size() <= MaximumTextSize;
}

[[nodiscard]] inline bool validOptionalText(const std::string_view value) noexcept {
  return value.size() <= MaximumTextSize;
}

[[nodiscard]] constexpr bool validMovementState(
    const ClientMovementState state) noexcept {
  const auto value = static_cast<std::uint16_t>(state);
  return (value & static_cast<std::uint16_t>(~KnownMovementStateMask)) == 0U;
}

[[nodiscard]] inline bool finiteSample(const ClientMovementSample& sample) noexcept {
  return std::isfinite(sample.x) && std::isfinite(sample.y) &&
         std::isfinite(sample.z) && std::isfinite(sample.yaw);
}

[[nodiscard]] inline bool validMovementIntent(
    const ClientMovementIntent& intent) noexcept {
  return intent.to.tick >= intent.from.tick && finiteSample(intent.from) &&
         finiteSample(intent.to) && validMovementState(intent.from.state) &&
         validMovementState(intent.to.state) &&
         std::isfinite(intent.cadence.minimumDistance) &&
         std::isfinite(intent.cadence.minimumYawDegrees) &&
         intent.cadence.minimumDistance >= 0.0 &&
         intent.cadence.minimumYawDegrees >= 0.0 &&
         validRequiredText(intent.targetKey) && validRequiredText(intent.actorKey) &&
         validRequiredText(intent.characterKey) && validRequiredText(intent.world) &&
         validOptionalText(intent.source) && validOptionalText(intent.waypointKey) &&
         validOptionalText(intent.reason);
}

[[nodiscard]] constexpr std::uint32_t movementStateFlags(
    const ClientMovementState state,
    const bool fromSample) noexcept {
  std::uint32_t flags = 0;
  const auto add = [&](const ClientMovementState value,
                       const std::uint32_t fromFlag,
                       const std::uint32_t toFlag) constexpr {
    if(hasMovementState(state, value))
      flags |= fromSample ? fromFlag : toFlag;
  };

  add(ClientMovementState::InAir, Net::ClientMovementFromInAir,
      Net::ClientMovementToInAir);
  add(ClientMovementState::Falling, Net::ClientMovementFromFalling,
      Net::ClientMovementToFalling);
  add(ClientMovementState::FallingDeep, Net::ClientMovementFromFallingDeep,
      Net::ClientMovementToFallingDeep);
  add(ClientMovementState::Sliding, Net::ClientMovementFromSlide,
      Net::ClientMovementToSlide);
  add(ClientMovementState::Jumping, Net::ClientMovementFromJump,
      Net::ClientMovementToJump);
  add(ClientMovementState::JumpingUp, Net::ClientMovementFromJumpUp,
      Net::ClientMovementToJumpUp);
  add(ClientMovementState::Swimming, Net::ClientMovementFromSwim,
      Net::ClientMovementToSwim);
  add(ClientMovementState::Diving, Net::ClientMovementFromDive,
      Net::ClientMovementToDive);
  add(ClientMovementState::InWater, Net::ClientMovementFromInWater,
      Net::ClientMovementToInWater);
  return flags;
}

[[nodiscard]] inline std::optional<Net::ClientMovementPacket>
makeCompatibilityMovementPacket(const ClientMovementIntent& intent) {
  if(!validMovementIntent(intent))
    return std::nullopt;

  Net::ClientMovementPacket packet;
  packet.kind = SemanticActionKind::MovementProposal;
  packet.flags = Net::ClientMovementHasFromTransform |
                 Net::ClientMovementHasToTransform |
                 movementStateFlags(intent.from.state, true) |
                 movementStateFlags(intent.to.state, false);
  packet.clientTick = intent.to.tick;
  packet.fromTick = intent.from.tick;
  packet.toTick = intent.to.tick;
  packet.deltaMs = intent.to.tick - intent.from.tick;
  packet.fromX = intent.from.x;
  packet.fromY = intent.from.y;
  packet.fromZ = intent.from.z;
  packet.fromYaw = intent.from.yaw;
  packet.toX = intent.to.x;
  packet.toY = intent.to.y;
  packet.toZ = intent.to.z;
  packet.toYaw = intent.to.yaw;
  packet.cadenceIntervalMs = intent.cadence.intervalMs;
  packet.cadenceMinDistance = intent.cadence.minimumDistance;
  packet.cadenceMinYawDeg = intent.cadence.minimumYawDegrees;
  packet.targetKey = std::string(intent.targetKey);
  packet.source = std::string(intent.source);
  packet.actorKey = std::string(intent.actorKey);
  packet.characterKey = std::string(intent.characterKey);
  packet.world = std::string(intent.world);
  packet.waypointKey = std::string(intent.waypointKey);
  packet.reason = std::string(intent.reason);
  return packet;
}

} // namespace Mmo::ClientAdapterDetail
