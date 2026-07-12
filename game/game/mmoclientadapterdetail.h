#pragma once

#include "mmoclientadapter.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

[[nodiscard]] inline bool validRequiredText(
    const std::string_view value) noexcept {
  return !value.empty() && value.size() <= MaximumTextSize;
}

[[nodiscard]] inline bool validOptionalText(
    const std::string_view value) noexcept {
  return value.size() <= MaximumTextSize;
}

[[nodiscard]] inline bool finitePosition(
    const ClientPosition& position) noexcept {
  return std::isfinite(position.x) && std::isfinite(position.y) &&
         std::isfinite(position.z);
}

[[nodiscard]] constexpr bool validMovementState(
    const ClientMovementState state) noexcept {
  const auto value = static_cast<std::uint16_t>(state);
  return (value & static_cast<std::uint16_t>(~KnownMovementStateMask)) == 0U;
}

[[nodiscard]] constexpr bool knownMovementRequestKind(
    const ClientMovementRequestKind kind) noexcept {
  switch(kind) {
    case ClientMovementRequestKind::MovementProposal:
    case ClientMovementRequestKind::CharacterCheckpoint:
      return true;
  }
  return false;
}

[[nodiscard]] inline bool finiteSample(
    const ClientMovementSample& sample) noexcept {
  return std::isfinite(sample.x) && std::isfinite(sample.y) &&
         std::isfinite(sample.z) && std::isfinite(sample.yaw);
}

[[nodiscard]] inline bool validMovementIntent(
    const ClientMovementIntent& intent) noexcept {
  return knownMovementRequestKind(intent.kind) &&
         intent.to.tick >= intent.from.tick && finiteSample(intent.from) &&
         finiteSample(intent.to) && validMovementState(intent.from.state) &&
         validMovementState(intent.to.state) &&
         std::isfinite(intent.cadence.minimumDistance) &&
         std::isfinite(intent.cadence.minimumYawDegrees) &&
         intent.cadence.minimumDistance >= 0.0 &&
         intent.cadence.minimumYawDegrees >= 0.0 &&
         validRequiredText(intent.targetKey) &&
         validRequiredText(intent.actorKey) &&
         validRequiredText(intent.characterKey) &&
         validRequiredText(intent.world) && validOptionalText(intent.source) &&
         validOptionalText(intent.waypointKey) &&
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
  packet.checkpointForceIntervalMs = intent.checkpointForceIntervalMs;
  packet.targetKey = std::string(intent.targetKey);
  packet.source = std::string(intent.source);
  packet.actorKey = std::string(intent.actorKey);
  packet.characterKey = std::string(intent.characterKey);
  packet.world = std::string(intent.world);
  packet.waypointKey = std::string(intent.waypointKey);
  packet.reason = std::string(intent.reason);

  switch(intent.kind) {
    case ClientMovementRequestKind::MovementProposal:
      packet.kind = SemanticActionKind::MovementProposal;
      packet.flags = Net::ClientMovementHasFromTransform |
                     Net::ClientMovementHasToTransform |
                     movementStateFlags(intent.from.state, true) |
                     movementStateFlags(intent.to.state, false);
      break;
    case ClientMovementRequestKind::CharacterCheckpoint:
      packet.kind = SemanticActionKind::CharacterCheckpoint;
      packet.flags = Net::ClientMovementHasToTransform;
      break;
  }
  return packet;
}

[[nodiscard]] inline bool validBootstrapRequest(
    const ClientBootstrapRequest& request) noexcept {
  return validRequiredText(request.targetKey) &&
         validRequiredText(request.actorKey) &&
         validRequiredText(request.characterKey) &&
         validRequiredText(request.world) &&
         validRequiredText(request.serverEndpoint) &&
         validOptionalText(request.source) &&
         validOptionalText(request.displayName) &&
         validOptionalText(request.clientContentManifestHash) &&
         validOptionalText(request.reason);
}

[[nodiscard]] inline std::optional<Net::ClientSessionControlPacket>
makeCompatibilityBootstrapPacket(const ClientBootstrapRequest& request) {
  if(!validBootstrapRequest(request))
    return std::nullopt;

  Net::ClientSessionControlPacket packet;
  packet.kind = SemanticActionKind::ClientBootstrapRequest;
  packet.flags = Net::ClientSessionControlServerBoundClientMode;
  packet.clientTick = request.clientTick;
  packet.targetKey = std::string(request.targetKey);
  packet.source = std::string(request.source);
  packet.sourceLocation = packet.source;
  packet.actorKey = std::string(request.actorKey);
  packet.characterKey = std::string(request.characterKey);
  packet.displayName = std::string(request.displayName);
  packet.world = std::string(request.world);
  packet.serverEndpoint = std::string(request.serverEndpoint);
  packet.clientContentManifestHash =
      std::string(request.clientContentManifestHash);
  packet.reason = std::string(request.reason);
  return packet;
}

[[nodiscard]] constexpr bool knownInteractionVerb(
    const ClientInteractionVerb verb) noexcept {
  switch(verb) {
    case ClientInteractionVerb::Use:
    case ClientInteractionVerb::Open:
    case ClientInteractionVerb::Close:
    case ClientInteractionVerb::Activate:
    case ClientInteractionVerb::Talk:
    case ClientInteractionVerb::Loot:
    case ClientInteractionVerb::Sleep:
    case ClientInteractionVerb::Read:
    case ClientInteractionVerb::Lockpick:
      return true;
  }
  return false;
}

[[nodiscard]] inline bool validInteractionRequest(
    const ClientInteractionRequest& request) noexcept {
  return knownInteractionVerb(request.verb) &&
         finitePosition(request.actorPosition) &&
         (request.targetHandle.valid() || validRequiredText(request.targetKey)) &&
         validRequiredText(request.actorKey) &&
         validRequiredText(request.characterKey) &&
         validRequiredText(request.world) &&
         validOptionalText(request.targetKey) &&
         validOptionalText(request.source) && validOptionalText(request.reason);
}

[[nodiscard]] constexpr bool supportsCompatibilityInteraction(
    const ClientInteractionVerb verb) noexcept {
  return verb == ClientInteractionVerb::Use;
}

[[nodiscard]] inline std::optional<Net::ClientWorldStatePacket>
makeCompatibilityInteractionPacket(const ClientInteractionRequest& request) {
  if(!validInteractionRequest(request) ||
     !supportsCompatibilityInteraction(request.verb) ||
     !validRequiredText(request.targetKey)) {
    return std::nullopt;
  }

  Net::ClientWorldStatePacket packet;
  packet.kind = SemanticActionKind::UseInteractive;
  packet.flags = Net::ClientWorldStateHasActorPosition;
  packet.clientTick = request.clientTick;
  packet.targetKey = std::string(request.targetKey);
  packet.source = std::string(request.source);
  packet.actorKey = std::string(request.actorKey);
  packet.characterKey = std::string(request.characterKey);
  packet.world = std::string(request.world);
  packet.reason = std::string(request.reason);
  packet.interactiveKey = packet.targetKey;
  packet.entityKey = packet.targetKey;
  packet.slotId = request.localSlotId;
  packet.vobId = request.localVobId;
  packet.actorPosX = request.actorPosition.x;
  packet.actorPosY = request.actorPosition.y;
  packet.actorPosZ = request.actorPosition.z;
  return packet;
}

[[nodiscard]] constexpr bool knownInventoryAction(
    const ClientInventoryAction action) noexcept {
  switch(action) {
    case ClientInventoryAction::PickupWorldItem:
    case ClientInventoryAction::EquipCharacterItem:
    case ClientInventoryAction::UnequipCharacterItem:
    case ClientInventoryAction::TakeContainerItem:
    case ClientInventoryAction::LootNpcInventory:
    case ClientInventoryAction::DropCharacterItem:
    case ClientInventoryAction::TradeBuyFromNpc:
    case ClientInventoryAction::TradeSellToNpc:
    case ClientInventoryAction::ConsumeItem:
      return true;
  }
  return false;
}

[[nodiscard]] inline bool fitsWireInteger(
    const std::optional<std::uint64_t>& value) noexcept {
  return !value.has_value() ||
         *value <= static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max());
}

[[nodiscard]] inline std::int64_t wireInteger(
    const std::optional<std::uint64_t>& value) noexcept {
  return value.has_value() ? static_cast<std::int64_t>(*value) : -1;
}

[[nodiscard]] inline bool validInventoryActionFields(
    const ClientInventoryRequest& request) noexcept {
  const auto hasItem = request.itemSymbol.has_value();
  const auto hasSourceItem = request.sourceItemPersistentId.has_value();
  switch(request.action) {
    case ClientInventoryAction::PickupWorldItem:
      return hasItem && request.inventoryItemSymbol.has_value() &&
             request.sourceWorldItemPersistentId.has_value();
    case ClientInventoryAction::EquipCharacterItem:
    case ClientInventoryAction::UnequipCharacterItem:
      return hasItem && request.itemPersistentId.has_value() &&
             validRequiredText(request.equipmentSlot);
    case ClientInventoryAction::TakeContainerItem:
      return hasItem && hasSourceItem &&
             validRequiredText(request.sourceEntityKey) &&
             validRequiredText(request.sourceContainerKey) &&
             validRequiredText(request.containerKey);
    case ClientInventoryAction::LootNpcInventory:
      return hasItem && hasSourceItem &&
             validRequiredText(request.sourceNpcKey) &&
             validRequiredText(request.sourceEntityKey);
    case ClientInventoryAction::DropCharacterItem:
      return hasItem && request.itemPersistentId.has_value() &&
             request.worldItemPersistentId.has_value();
    case ClientInventoryAction::TradeBuyFromNpc:
      return hasItem && request.vendorItemPersistentId.has_value() &&
             validRequiredText(request.npcKey) &&
             validRequiredText(request.targetNpcEntityKey);
    case ClientInventoryAction::TradeSellToNpc:
      return hasItem && request.sellerItemPersistentId.has_value() &&
             validRequiredText(request.npcKey) &&
             validRequiredText(request.targetNpcEntityKey);
    case ClientInventoryAction::ConsumeItem:
      return hasItem && request.itemPersistentId.has_value();
  }
  return false;
}

[[nodiscard]] inline bool validInventoryRequest(
    const ClientInventoryRequest& request) noexcept {
  const auto amountFits =
      request.amount != 0U &&
      request.amount <= static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max());
  return knownInventoryAction(request.action) && amountFits &&
         finitePosition(request.actorPosition) &&
         fitsWireInteger(request.itemSymbol) &&
         fitsWireInteger(request.inventoryItemSymbol) &&
         fitsWireInteger(request.itemPersistentId) &&
         fitsWireInteger(request.sourceItemPersistentId) &&
         fitsWireInteger(request.sourceWorldItemPersistentId) &&
         fitsWireInteger(request.worldItemPersistentId) &&
         fitsWireInteger(request.vendorItemPersistentId) &&
         fitsWireInteger(request.sellerItemPersistentId) &&
         validRequiredText(request.targetKey) &&
         validRequiredText(request.actorKey) &&
         validRequiredText(request.itemTemplateKey) &&
         validRequiredText(request.world) && validOptionalText(request.source) &&
         validOptionalText(request.equipmentSlot) &&
         validOptionalText(request.sourceEntityKey) &&
         validOptionalText(request.sourceContainerKey) &&
         validOptionalText(request.containerKey) &&
         validOptionalText(request.sourceNpcKey) &&
         validOptionalText(request.targetNpcEntityKey) &&
         validOptionalText(request.npcKey) && validOptionalText(request.reason) &&
         validInventoryActionFields(request);
}

[[nodiscard]] constexpr SemanticActionKind inventoryActionKind(
    const ClientInventoryAction action) noexcept {
  switch(action) {
    case ClientInventoryAction::PickupWorldItem:
      return SemanticActionKind::PickupWorldItem;
    case ClientInventoryAction::EquipCharacterItem:
      return SemanticActionKind::EquipCharacterItem;
    case ClientInventoryAction::UnequipCharacterItem:
      return SemanticActionKind::UnequipCharacterItem;
    case ClientInventoryAction::TakeContainerItem:
      return SemanticActionKind::TakeContainerItem;
    case ClientInventoryAction::LootNpcInventory:
      return SemanticActionKind::LootNpcInventory;
    case ClientInventoryAction::DropCharacterItem:
      return SemanticActionKind::DropCharacterItem;
    case ClientInventoryAction::TradeBuyFromNpc:
      return SemanticActionKind::TradeBuyFromNpc;
    case ClientInventoryAction::TradeSellToNpc:
      return SemanticActionKind::TradeSellToNpc;
    case ClientInventoryAction::ConsumeItem:
      return SemanticActionKind::ConsumeItem;
  }
  return SemanticActionKind::PickupWorldItem;
}

[[nodiscard]] inline std::optional<Net::ClientInventoryPacket>
makeCompatibilityInventoryPacket(const ClientInventoryRequest& request) {
  if(!validInventoryRequest(request))
    return std::nullopt;

  Net::ClientInventoryPacket packet;
  packet.kind = inventoryActionKind(request.action);
  packet.flags = Net::ClientInventoryHasActorPosition;
  if(request.action == ClientInventoryAction::EquipCharacterItem ||
     request.action == ClientInventoryAction::UnequipCharacterItem) {
    packet.flags |= Net::ClientInventoryHasEquipmentSlot;
  }
  packet.clientTick = request.clientTick;
  packet.itemSymbol = wireInteger(request.itemSymbol);
  packet.inventoryItemSymbol = wireInteger(request.inventoryItemSymbol);
  packet.itemPersistentId = wireInteger(request.itemPersistentId);
  packet.sourceItemPersistentId = wireInteger(request.sourceItemPersistentId);
  packet.sourceWorldItemPersistentId =
      wireInteger(request.sourceWorldItemPersistentId);
  packet.worldItemPersistentId = wireInteger(request.worldItemPersistentId);
  packet.vendorItemPersistentId = wireInteger(request.vendorItemPersistentId);
  packet.sellerItemPersistentId = wireInteger(request.sellerItemPersistentId);
  packet.amount = static_cast<std::int64_t>(request.amount);
  packet.slot = request.equipmentSlotId;
  packet.actorPosX = request.actorPosition.x;
  packet.actorPosY = request.actorPosition.y;
  packet.actorPosZ = request.actorPosition.z;
  packet.targetKey = std::string(request.targetKey);
  packet.source = std::string(request.source);
  packet.actorKey = std::string(request.actorKey);
  packet.itemTemplateKey = std::string(request.itemTemplateKey);
  packet.equipmentSlot = std::string(request.equipmentSlot);
  packet.sourceEntityKey = std::string(request.sourceEntityKey);
  packet.sourceContainerKey = std::string(request.sourceContainerKey);
  packet.containerKey = std::string(request.containerKey);
  packet.sourceNpcKey = std::string(request.sourceNpcKey);
  packet.targetNpcEntityKey = std::string(request.targetNpcEntityKey);
  packet.npcKey = std::string(request.npcKey);
  packet.world = std::string(request.world);
  packet.reason = std::string(request.reason);
  return packet;
}

[[nodiscard]] constexpr bool knownWeaponStateIntent(
    const ClientWeaponStateIntent intent) noexcept {
  switch(intent) {
    case ClientWeaponStateIntent::Ready:
    case ClientWeaponStateIntent::Holster:
      return true;
  }
  return false;
}

[[nodiscard]] inline bool validWeaponStateRequest(
    const ClientWeaponStateRequest& request) noexcept {
  return knownWeaponStateIntent(request.intent) &&
         finitePosition(request.actorPosition) &&
         validRequiredText(request.targetKey) &&
         validRequiredText(request.actorKey) &&
         validRequiredText(request.characterKey) &&
         validRequiredText(request.world) && validOptionalText(request.source) &&
         validOptionalText(request.reason);
}

[[nodiscard]] inline std::optional<Net::ClientWorldStatePacket>
makeCompatibilityWeaponStatePacket(const ClientWeaponStateRequest& request) {
  if(!validWeaponStateRequest(request))
    return std::nullopt;

  Net::ClientWorldStatePacket packet;
  packet.kind = request.intent == ClientWeaponStateIntent::Holster
                    ? SemanticActionKind::HolsterWeapon
                    : SemanticActionKind::ReadyWeapon;
  packet.flags = Net::ClientWorldStateHasActorPosition;
  packet.clientTick = request.clientTick;
  packet.actorPosX = request.actorPosition.x;
  packet.actorPosY = request.actorPosition.y;
  packet.actorPosZ = request.actorPosition.z;
  packet.targetKey = std::string(request.targetKey);
  packet.source = std::string(request.source);
  packet.actorKey = std::string(request.actorKey);
  packet.characterKey = std::string(request.characterKey);
  packet.world = std::string(request.world);
  packet.reason = std::string(request.reason);
  return packet;
}

[[nodiscard]] inline bool validCombatRequest(
    const ClientCombatRequest& request) noexcept {
  return finitePosition(request.actorPosition) &&
         validRequiredText(request.targetKey) &&
         validRequiredText(request.actorKey) &&
         validRequiredText(request.npcEntityKey) &&
         validRequiredText(request.world) &&
         validRequiredText(request.combatAction) &&
         validRequiredText(request.intentState) &&
         validOptionalText(request.source) && validOptionalText(request.reason) &&
         validOptionalText(request.targetNpcEntityKey);
}

[[nodiscard]] inline std::optional<Net::ClientNpcStatePacket>
makeCompatibilityCombatPacket(const ClientCombatRequest& request) {
  if(!validCombatRequest(request))
    return std::nullopt;

  Net::ClientNpcStatePacket packet;
  packet.kind = SemanticActionKind::RecordCombatIntent;
  packet.flags = Net::ClientNpcStateHasPosition;
  packet.clientTick = request.clientTick;
  packet.targetKey = std::string(request.targetKey);
  packet.source = std::string(request.source);
  packet.reason = std::string(request.reason);
  packet.actorKey = std::string(request.actorKey);
  packet.npcEntityKey = std::string(request.npcEntityKey);
  packet.npcKey = packet.npcEntityKey;
  packet.targetNpcEntityKey = std::string(request.targetNpcEntityKey);
  packet.targetNpcKey = packet.targetNpcEntityKey;
  packet.world = std::string(request.world);
  packet.combatAction = std::string(request.combatAction);
  packet.intentState = std::string(request.intentState);
  packet.posX = request.actorPosition.x;
  packet.posY = request.actorPosition.y;
  packet.posZ = request.actorPosition.z;
  return packet;
}

[[nodiscard]] inline bool validDialogChoiceRequest(
    const ClientDialogChoiceRequest& request) noexcept {
  return request.expectedRevision != 0U &&
         request.clientChoiceSequence != 0U &&
         validRequiredText(request.sessionUuid) &&
         validRequiredText(request.characterKey) &&
         validRequiredText(request.conversationId) &&
         validRequiredText(request.choiceId);
}

[[nodiscard]] inline std::optional<Net::ClientDialogChoiceIntentPacket>
makeCompatibilityDialogChoicePacket(
    const ClientDialogChoiceRequest& request,
    const std::uint64_t localSequence,
    const std::string_view sessionKey) {
  if(!validDialogChoiceRequest(request) || localSequence == 0U ||
     !validRequiredText(sessionKey)) {
    return std::nullopt;
  }

  Net::ClientDialogChoiceIntentPacket packet;
  packet.localSequence = localSequence;
  packet.clientTick = request.clientTick;
  packet.expectedRevision = request.expectedRevision;
  packet.clientChoiceSequence = request.clientChoiceSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.sessionUuid = std::string(request.sessionUuid);
  packet.characterKey = std::string(request.characterKey);
  packet.conversationId = std::string(request.conversationId);
  packet.choiceId = std::string(request.choiceId);
  packet.idempotencyKey = packet.sessionUuid + ":" + packet.conversationId +
                          ":" + std::to_string(request.clientChoiceSequence);
  return packet;
}

} // namespace Mmo::ClientAdapterDetail
